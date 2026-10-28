#include "SimulatedNordLead2X.h"

#include "nl2x/HardwareAssumptions.h"

#include <algorithm>
#include <cassert>
#include <utility>
#include <vector>

namespace nl2x::test {

namespace {

// CC number → decoded Program offset, from the CC map in research §2 [OFF-2X p.103–104,
// 111–112]. Grows with the engine's parameter table (#6).
constexpr std::array<std::pair<std::uint8_t, std::size_t>, 1> kCcToProgramOffset { {
    { 74, 3 }, // Filter Cutoff
} };

constexpr std::uint8_t kControlChange = 0xB0;

std::optional<Slot> slotFromSpec(std::uint8_t spec)
{
    if (spec >= kAllSlots.size())
        return std::nullopt;
    return kAllSlots[spec];
}

} // namespace

SimulatedNordLead2X::SimulatedNordLead2X()
    : globalChannel(hardware::kFactoryGlobalChannel)
{
    for (auto slot : kAllSlots)
        slots[slotIndex(slot)].channel = hardware::kDefaultSlotChannels[slotIndex(slot)];
}

void SimulatedNordLead2X::receive(std::span<const std::uint8_t> message, TimeMs timeMs)
{
    now = timeMs;
    receive(message);
}

void SimulatedNordLead2X::receive(std::span<const std::uint8_t> message)
{
    if (message.size() == 3 && (message[0] & 0xF0) == kControlChange)
        handleControlChange(static_cast<MidiChannel>(message[0] & 0x0F), message[1], message[2]);
    else if (! message.empty() && message[0] == kSysExStart)
        handleSysEx(message);
}

void SimulatedNordLead2X::turnPanelControl(Slot slot, std::uint8_t cc, std::uint8_t value)
{
    const auto& state = slots[slotIndex(slot)];
    if (! state.channel)
        return;

    handleControlChange(*state.channel, cc, value);
    const std::array<std::uint8_t, 3> message { static_cast<std::uint8_t>(kControlChange | *state.channel), cc, value };
    transmit(message);
}

void SimulatedNordLead2X::setEditBuffer(Slot slot, const Program& program)
{
    auto& state = slots[slotIndex(slot)];
    state.editBuffer = program;
    state.percussionKit.reset();
}

const SimulatedNordLead2X::Program& SimulatedNordLead2X::bankProgram(int shownBank, int location) const
{
    return banks.at(static_cast<std::size_t>(shownBank)).at(static_cast<std::size_t>(location));
}

void SimulatedNordLead2X::setBankProgram(int shownBank, int location, const Program& program)
{
    banks.at(static_cast<std::size_t>(shownBank)).at(static_cast<std::size_t>(location)) = program;
}

void SimulatedNordLead2X::handleControlChange(MidiChannel channel, std::uint8_t cc, std::uint8_t value)
{
    const auto* entry = std::find_if(kCcToProgramOffset.begin(), kCcToProgramOffset.end(),
                                     [cc](const auto& e) { return e.first == cc; });
    if (entry == kCcToProgramOffset.end())
        return;

    // Every Slot on the channel reacts [OFF-2X p.77].
    for (auto& state : slots)
        if (state.channel == channel)
            state.editBuffer[entry->second] = static_cast<std::int8_t>(value);
}

void SimulatedNordLead2X::handleSysEx(std::span<const std::uint8_t> message)
{
    if (! answersSysEx)
        return;

    const auto previous = lastSysExTime;
    lastSysExTime = now;
    if (previous && now - *previous < sysExRecoveryMs) {
        ++lostSysEx;
        return;
    }

    const auto parsed = parseSysEx(message);
    if (parsed.kind == SysExKind::NotNordLead || parsed.globalChannel != globalChannel || ! parsed.values)
        return;

    if (parsed.values->empty())
        handleRequest(parsed.type, parsed.spec);
    else
        handleDump(parsed);
}

void SimulatedNordLead2X::handleRequest(std::uint8_t type, std::uint8_t spec)
{
    if (type == hardware::kSlotEditBufferRequestType) {
        if (const auto slot = slotFromSpec(spec))
            sendSlotEditBuffer(*slot);
        return;
    }

    if (type == hardware::kAllControllersRequestType) {
        if (const auto slot = slotFromSpec(spec))
            sendAllControllers(*slot);
        return;
    }

    if (type == hardware::kPerformanceEditBufferRequestType) {
        sendPerformance();
        return;
    }

    if (bankRequestType && type >= *bankRequestType && type < *bankRequestType + kRamBankCount
        && spec < kProgramsPerBank) {
        const auto sysExBank = type - *bankRequestType + 1;
        const auto shownBank = sysExBank - 1 + hardware::kShownBankOfSysExBank1;
        transmit(bankProgramDump(globalChannel, static_cast<std::uint8_t>(sysExBank), spec,
                                 bankProgram(shownBank, spec)));
    }
}

void SimulatedNordLead2X::sendSlotEditBuffer(Slot requested)
{
    const auto slot = hardware::kSlotEditBufferReplyFollowsRequestedSlot ? requested : activeSlot;
    const auto& state = slots[slotIndex(slot)];
    const auto spec = static_cast<std::uint8_t>(slotIndex(slot));
    // A Kit answers with a Kit dump, whose size is what tells it apart (HardwareAssumptions.h).
    if (state.percussionKit)
        transmit(buildSysEx(globalChannel, hardware::kSlotEditBufferReplyType,
                            static_cast<std::uint8_t>(hardware::kSlotEditBufferKitReplySpecOfSlotA + spec),
                            *state.percussionKit));
    else
        transmit(buildSysEx(globalChannel, hardware::kSlotEditBufferReplyType, spec, state.editBuffer));
}

void SimulatedNordLead2X::handleDump(const ParsedSysEx& dump)
{
    const auto& values = *dump.values;

    if (dump.kind == SysExKind::ProgramDump) {
        Program program;
        std::copy(values.begin(), values.end(), program.begin());
        if (dump.type == hardware::kProgramEditBufferDumpType) {
            // A Program is received only into a Slot playing a Program [OFF-2X p.30].
            if (const auto slot = slotFromSpec(dump.spec); slot && ! playsPercussionKit(*slot))
                slots[slotIndex(*slot)].editBuffer = program;
        } else if (dump.type >= hardware::kFirstProgramBankDumpType && dump.type <= hardware::kLastProgramBankDumpType
                   && dump.spec < kProgramsPerBank) {
            // The type byte is ignored: stored in the Bank selected on the panel [OFF-2X p.78].
            setBankProgram(selectedBank, dump.spec, program);
        }
        return;
    }

    if (dump.kind == SysExKind::PercussionKitDump && dump.type == hardware::kProgramEditBufferDumpType
        && dump.spec >= hardware::kPercussionKitEditBufferSpecOfSlotA) {
        // A Kit is received only into a Slot playing a Kit [OFF-2X p.30].
        const auto slot = slotFromSpec(static_cast<std::uint8_t>(dump.spec - hardware::kPercussionKitEditBufferSpecOfSlotA));
        if (slot && playsPercussionKit(*slot))
            std::copy(values.begin(), values.end(), slots[slotIndex(*slot)].percussionKit->begin());
        return;
    }

    if (dump.kind == SysExKind::PerformanceDump && dump.type == hardware::kPerformanceEditBufferDumpType) {
        for (auto slot : kAllSlots) {
            const auto first = values.begin() + static_cast<std::ptrdiff_t>(slotIndex(slot) * kProgramParameterCount);
            std::copy(first, first + static_cast<std::ptrdiff_t>(kProgramParameterCount),
                      slots[slotIndex(slot)].editBuffer.begin());
        }
        const auto settingsBegin = values.begin() + static_cast<std::ptrdiff_t>(kPerformanceSlotChannelOffset);
        std::copy(settingsBegin, values.end(), settings.begin());
        // [INF] −1 = off (research §3, Performance layout).
        for (auto slot : kAllSlots) {
            const auto channel = settings[slotIndex(slot)];
            slots[slotIndex(slot)].channel = channel >= 0 && channel < 16
                                                 ? std::optional<MidiChannel>(static_cast<MidiChannel>(channel))
                                                 : std::nullopt;
        }
    }
}

void SimulatedNordLead2X::sendAllControllers(Slot slot)
{
    const auto& state = slots[slotIndex(slot)];
    if (! state.channel)
        return;

    for (const auto& [cc, offset] : kCcToProgramOffset) {
        const std::array<std::uint8_t, 3> message { static_cast<std::uint8_t>(kControlChange | *state.channel), cc,
                                                    static_cast<std::uint8_t>(state.editBuffer[offset] & 0x7F) };
        transmit(message);
    }
}

void SimulatedNordLead2X::sendPerformance()
{
    std::vector<std::int8_t> performance;
    performance.reserve(kPerformanceParameterCount);
    for (const auto& state : slots)
        performance.insert(performance.end(), state.editBuffer.begin(), state.editBuffer.end());

    auto current = settings;
    for (auto slot : kAllSlots) {
        const auto& channel = slots[slotIndex(slot)].channel;
        current[slotIndex(slot)] = channel ? static_cast<std::int8_t>(*channel) : std::int8_t { -1 };
    }
    performance.insert(performance.end(), current.begin(), current.end());
    assert(performance.size() == kPerformanceParameterCount);

    transmit(performanceEditBufferDump(globalChannel, performance));
}

void SimulatedNordLead2X::transmit(std::span<const std::uint8_t> message)
{
    if (midiOut != nullptr)
        midiOut->send(message);
}

} // namespace nl2x::test
