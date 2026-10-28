#include "nl2x/Engine.h"

#include "PerformanceEditBuffer.h"
#include "ProgramDumpThrottle.h"
#include "SelectionTracker.h"
#include "ConnectionCheck.h"
#include "SynthConnection.h"

#include "nl2x/HardwareAssumptions.h"
#include "nl2x/Program.h"

#include <algorithm>

namespace nl2x {

namespace {

constexpr std::uint8_t kControlChange = 0xB0;
constexpr GlobalChannel kGlobalChannelCount = 16;

// The remembered Global Channel first (connecting is fast when nothing changed), then the one a
// synth with factory settings uses, then the rest in order.
std::vector<GlobalChannel> probeOrder(std::optional<GlobalChannel> remembered, std::optional<GlobalChannel> found,
                                      std::optional<GlobalChannel> manual)
{
    // A manual Global Channel is never probed around (spec #1, user story 4).
    if (manual)
        return { *manual };
    std::vector<GlobalChannel> order;
    const auto add = [&order](GlobalChannel channel) {
        if (channel < kGlobalChannelCount && std::find(order.begin(), order.end(), channel) == order.end())
            order.push_back(channel);
    };
    if (remembered)
        add(*remembered);
    if (found)
        add(*found);
    add(hardware::kFactoryGlobalChannel);
    for (GlobalChannel channel = 0; channel < kGlobalChannelCount; ++channel)
        add(channel);
    return order;
}

// A reply to a Slot Edit Buffer request: matched by size, never by header (HardwareAssumptions.h),
// and only when its data can be decoded without loss.
bool isSlotEditBufferReply(std::span<const std::uint8_t> message)
{
    if (hardware::classifySlotEditBufferReply(message.size()) == hardware::SlotEditBufferReply::NotAReply)
        return false;
    const auto parsed = parseSysEx(message);
    return parsed.kind != SysExKind::NotNordLead && parsed.values.has_value();
}

} // namespace

Engine::Engine(MidiOutput& midiOutput)
    : Engine(midiOutput, hardware::Switches {})
{
}

Engine::Engine(MidiOutput& midiOutput, hardware::Switches hardwareSwitches)
    : output(midiOutput),
      connection(std::make_unique<SynthConnection>(midiOutput)),
      performanceBuffer(std::make_unique<PerformanceEditBuffer>()),
      selectionTracker(std::make_unique<SelectionTracker>()),
      programDumps(std::make_unique<ProgramDumpThrottle>()),
      switches(hardwareSwitches),
      check(std::make_unique<ConnectionCheck>(*connection, midiOutput, [this] { changed(); })),
      memory(std::make_unique<MemoryWriter>(*this, *connection, hardwareSwitches, [this] { changed(); }))
{
}

Engine::~Engine() = default;

void Engine::handleIncomingMidi(std::span<const std::uint8_t> message, TimeMs timeMs)
{
    if (message.size() == 3 && (message[0] & 0xF0) == kControlChange)
        applyControlChange(static_cast<MidiChannel>(message[0] & 0x0F), message[1], message[2]);
    check->handleIncomingMidi(message, timeMs);
    connection->handleIncomingMidi(message, timeMs);
    followSelectionOnSynth(message, timeMs);
    // Someone selected a sound on the synth: the selected Bank may have changed under a write.
    const bool programChange = message.size() == 2 && (message[0] & 0xF0) == 0xC0;
    const bool bankSelect = message.size() == 3 && (message[0] & 0xF0) == kControlChange
                            && message[1] == hardware::kBankSelectController;
    if (programChange || bankSelect)
        memory->selectionChangedOnSynth();
}

void Engine::advanceTime(TimeMs timeMs)
{
    rereadAfterSelection(timeMs);
    connection->advanceTime(timeMs);
    memory->advanceTime(timeMs);
    // Edits wait until no request is in flight, so a dump never interrupts a reply.
    if (foundGlobalChannel && connection->isIdle(timeMs))
        if (const auto dump = performanceBuffer->takeDueDump(timeMs, *foundGlobalChannel, slots))
            connection->sendWithoutReply(*dump, timeMs);
    sendDueAudition(timeMs);
    sendDueProgramDumps(timeMs);
    check->advanceTime(timeMs);
}

void Engine::setParameter(Slot slot, CcParameter parameter, int value)
{
    if (parameter == CcParameter::RingModSync && ! switches.ringModReachableByCc35) {
        setRingModSyncWithoutRingModCc(slot, value);
        return;
    }
    auto& state = slots[slotIndex(slot)];
    if (state.program && ccParameterValue(*state.program, parameter) != value) {
        setCcParameterValue(*state.program, parameter, value);
        changed();
    }

    const auto channel = slotChannel(slot);
    if (! channel)
        return;
    const std::array<std::uint8_t, 3> message { static_cast<std::uint8_t>(kControlChange | *channel),
                                                ccParameterInfo(parameter).cc, ccDataOf(parameter, value) };
    output.send(message);
}

void Engine::applyControlChange(MidiChannel channel, std::uint8_t cc, std::uint8_t data)
{
    const auto parameter = ccParameterOfController(cc);
    if (! parameter)
        return;
    if (*parameter == CcParameter::RingModSync && ! switches.ringModReachableByCc35) {
        applySyncOnlyControlChange(channel, data);
        return;
    }
    const auto value = valueOfCcData(*parameter, data);
    if (! value)
        return;

    SlotChannelArray channels;
    for (auto slot : kAllSlots)
        channels[slotIndex(slot)] = slotChannel(slot);
    for (auto slot : slotsReceivingOn(channel, channels, performanceBuffer->layer())) {
        // A Kit Slot's CCs reach the percussion sound last selected on the synth [OFF-2X p.103],
        // which the editor does not know; an unread Slot has nothing to update.
        auto& program = slots[slotIndex(slot)].program;
        if (program && ccParameterValue(*program, *parameter) != *value) {
            setCcParameterValue(*program, *parameter, *value);
            changed();
        }
    }
}

void Engine::connect(std::optional<GlobalChannel> rememberedGlobalChannel, TimeMs timeMs)
{
    check->cancel();
    memory->abort("The connection to the synth was reset.");
    connection->clear();
    pendingSlotReads = 0;
    slotReadFailed = false;
    performanceBuffer->forget();
    pendingAuditions.clear();
    programDumps->dropAllUnsentEdits();
    changed();
    setStatus(ConnectionStatus::SearchingGlobalChannel);
    probe(probeOrder(rememberedGlobalChannel, foundGlobalChannel, manualChannel), 0, Slot::A, true, timeMs);
}

void Engine::readSlot(Slot slot, TimeMs timeMs)
{
    if (status == ConnectionStatus::SearchingGlobalChannel)
        return;

    if (! foundGlobalChannel) {
        setStatus(ConnectionStatus::SearchingGlobalChannel);
        probe(probeOrder(std::nullopt, std::nullopt, manualChannel), 0, slot, false, timeMs);
        return;
    }

    if (pendingSlotReads == 0)
        slotReadFailed = false;
    setStatus(ConnectionStatus::ReadingSlots);
    queueSlotRead(slot, timeMs);
}

void Engine::readPerformance(TimeMs timeMs)
{
    if (status == ConnectionStatus::SearchingGlobalChannel)
        return;
    if (! foundGlobalChannel) {
        connect(std::nullopt, timeMs);
        return;
    }
    if (performanceBuffer->status() != PerformanceStatus::Reading)
        queuePerformanceRead(timeMs);
}

PerformanceEditResult Engine::setPerformanceSetting(PerformanceSetting setting, Slot slot, int value)
{
    const auto before = performanceBuffer->performance();
    const auto result = performanceBuffer->edit(setting, slot, value);
    if (performanceBuffer->performance() != before)
        changed();
    return result;
}

void Engine::setSlotChannelOverride(Slot slot, std::optional<MidiChannel> channel)
{
    if (channel && *channel > 15)
        channel.reset();
    if (channelOverrides[slotIndex(slot)] != channel) {
        channelOverrides[slotIndex(slot)] = channel;
        changed();
    }
}

PerformanceStatus Engine::performanceStatus() const
{
    return performanceBuffer->status();
}

const std::optional<Performance>& Engine::performance() const
{
    return performanceBuffer->performance();
}

std::optional<MidiChannel> Engine::slotChannel(Slot slot) const
{
    if (const auto& pinned = channelOverrides[slotIndex(slot)])
        return pinned;
    if (performanceBuffer->performance())
        return performanceBuffer->slotChannel(slot);
    return hardware::kDefaultSlotChannels[slotIndex(slot)];
}

std::vector<SlotChannelWarning> Engine::slotChannelWarnings() const
{
    SlotChannelArray channels;
    for (auto slot : kAllSlots)
        channels[slotIndex(slot)] = slotChannel(slot);
    return nl2x::slotChannelWarnings(channels, performanceBuffer->layer());
}

void Engine::probe(std::vector<GlobalChannel> candidates, std::size_t index, Slot slot, bool readOtherSlots,
                   TimeMs timeMs)
{
    const auto channel = candidates[index];
    connection->enqueue(
        slotEditBufferRequest(channel, slot), isSlotEditBufferReply,
        [this, candidates, index, slot, readOtherSlots, channel](auto reply, TimeMs doneMs) {
            if (! reply) {
                if (index + 1 < candidates.size())
                    probe(candidates, index + 1, slot, readOtherSlots, doneMs);
                else
                    setStatus(ConnectionStatus::NoAnswer);
                return;
            }

            if (foundGlobalChannel != channel) {
                foundGlobalChannel = channel;
                changed();
            }
            applySlotReply(slot, *reply);

            if (! readOtherSlots) {
                setStatus(ConnectionStatus::Connected);
            } else {
                setStatus(ConnectionStatus::ReadingSlots);
                for (auto other : kAllSlots)
                    if (other != slot)
                        queueSlotRead(other, doneMs);
            }
            // The Global Channel is known now: the Slot Channels come from the Performance.
            queuePerformanceRead(doneMs);
        },
        timeMs);
}

void Engine::queueSlotRead(Slot slot, TimeMs timeMs)
{
    ++pendingSlotReads;
    connection->enqueue(slotEditBufferRequest(*foundGlobalChannel, slot), isSlotEditBufferReply,
                        [this, slot](auto reply, TimeMs) {
                            --pendingSlotReads;
                            if (reply)
                                applySlotReply(slot, *reply);
                            else
                                slotReadFailed = true;
                            if (pendingSlotReads == 0)
                                setStatus(slotReadFailed ? ConnectionStatus::NoAnswer : ConnectionStatus::Connected);
                        },
                        timeMs);
}

void Engine::queuePerformanceRead(TimeMs timeMs)
{
    performanceBuffer->startReading();
    changed();
    connection->enqueue(performanceEditBufferRequest(*foundGlobalChannel), PerformanceEditBuffer::isReply,
                        [this](auto reply, TimeMs) {
                            // Its own status: a synth that answers Slot requests but not this one
                            // is still connected (the type-40 request is unverified, research §7.3).
                            if (reply) {
                                performanceBuffer->applyReply(*reply);
                                takeSelectionFromPerformance();
                            } else
                                performanceBuffer->readFailed();
                            changed();
                        },
                        timeMs);
}

void Engine::applySlotReply(Slot slot, std::span<const std::uint8_t> reply)
{
    SlotState state;
    switch (hardware::classifySlotEditBufferReply(reply.size())) {
    case hardware::SlotEditBufferReply::Program:
        if (const auto dump = parseProgramDump(reply)) {
            state.content = SlotContent::Program;
            state.program = dump->program;
            programDumps->applyUnsentEdits(slot, *state.program);
        }
        break;
    case hardware::SlotEditBufferReply::PercussionKit:
        state.content = SlotContent::PercussionKit;
        programDumps->dropUnsentEdits(slot);
        break;
    case hardware::SlotEditBufferReply::NotAReply:
        break;
    }

    auto& current = slots[slotIndex(slot)];
    if (current.content != state.content || current.program != state.program) {
        current = state;
        changed();
    }
}

void Engine::setStatus(ConnectionStatus newStatus)
{
    if (status != newStatus) {
        status = newStatus;
        changed();
    }
}

} // namespace nl2x
