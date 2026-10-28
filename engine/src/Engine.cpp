#include "nl2x/Engine.h"

#include "PerformanceEditBuffer.h"
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
    : output(midiOutput),
      connection(std::make_unique<SynthConnection>(midiOutput)),
      performanceBuffer(std::make_unique<PerformanceEditBuffer>()),
      check(std::make_unique<ConnectionCheck>(*connection, midiOutput, [this] { changed(); }))
{
}

Engine::~Engine() = default;

void Engine::handleIncomingMidi(std::span<const std::uint8_t> message, TimeMs timeMs)
{
    // Incoming CCs (#6) come next; for now only replies to requests count.
    check->handleIncomingMidi(message, timeMs);
    connection->handleIncomingMidi(message, timeMs);
}

void Engine::advanceTime(TimeMs timeMs)
{
    connection->advanceTime(timeMs);
    // Edits wait until no request is in flight, so a dump never interrupts a reply.
    if (foundGlobalChannel && connection->isIdle(timeMs))
        if (const auto dump = performanceBuffer->takeDueDump(timeMs, *foundGlobalChannel, slots))
            connection->sendWithoutReply(*dump, timeMs);
    check->advanceTime(timeMs);
}

void Engine::setParameter(Slot slot, ProgramParameter parameter, int value)
{
    auto& state = slots[slotIndex(slot)];
    if (state.program && state.program->value(parameter) != value) {
        state.program->setValue(parameter, value);
        changed();
    }

    const auto& info = programParameterInfo(parameter);
    const auto channel = slotChannel(slot);
    if (! info.cc || ! channel)
        return;

    // A CC data byte has 7 bits.
    const auto data = static_cast<std::uint8_t>(std::clamp(value, 0, 127));
    const std::array<std::uint8_t, 3> message { static_cast<std::uint8_t>(kControlChange | *channel), *info.cc, data };
    output.send(message);
}

void Engine::connect(std::optional<GlobalChannel> rememberedGlobalChannel, TimeMs timeMs)
{
    check->cancel();
    connection->clear();
    pendingSlotReads = 0;
    slotReadFailed = false;
    performanceBuffer->forget();
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
                            if (reply)
                                performanceBuffer->applyReply(*reply);
                            else
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
        }
        break;
    case hardware::SlotEditBufferReply::PercussionKit:
        state.content = SlotContent::PercussionKit;
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
