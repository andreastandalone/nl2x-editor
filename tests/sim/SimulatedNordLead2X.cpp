#include "SimulatedNordLead2X.h"

#include "nl2x/HardwareAssumptions.h"

#include <algorithm>
#include <cassert>
#include <utility>
#include <vector>

namespace nl2x::test {

namespace {

// The panel CCs, written from the CC chart in research §2 [OFF-2X p.103–104] and the patch
// layout in research §3 [OFF-2X p.111–112], independently of the engine's table. Byte 52 holds
// two controls: the RING MOD/SYNC button in bits 0–1 and Filter Distortion in bit 4. How a CC
// value becomes a byte follows HardwareAssumptions.h, as on the real synth.
struct PanelCc {
    std::uint8_t cc;
    std::size_t offset;
    // The bits of the byte; 0xFF = all of it.
    std::uint8_t bits;
    bool isSwitch;
};

constexpr std::uint8_t kAll = 0xFF;

constexpr std::array<PanelCc, 42> kPanelCcs { {
    { 7, 17, kAll, false },  // Gain
    { 8, 2, kAll, false },   // Osc Mix
    { 5, 16, kAll, false },  // Portamento Time
    { 15, 58, kAll, false }, // Poly/Legato/Mono
    { 16, 60, kAll, true },  // Unison
    { 17, 64, kAll, false }, // Oct Shift
    { 18, 59, kAll, false }, // Mod Wheel Destination
    { 19, 21, kAll, false }, // LFO1 Rate
    { 20, 56, kAll, false }, // LFO1 Waveform
    { 21, 57, kAll, false }, // LFO1 Destination
    { 22, 22, kAll, false }, // LFO1 Amount
    { 23, 23, kAll, false }, // LFO2/Arp Rate
    { 24, 65, kAll, false }, // LFO2 Dest/Arp Mode
    { 25, 24, kAll, false }, // LFO2 Amount/Arp Range
    { 26, 18, kAll, false }, // Mod Env Attack
    { 27, 19, kAll, false }, // Mod Env Decay
    { 28, 61, kAll, false }, // Mod Env Destination
    { 29, 20, kAll, false }, // Mod Env Amount
    { 30, 50, kAll, false }, // Osc1 Waveform
    { 31, 51, kAll, false }, // Osc2 Waveform
    { 33, 1, kAll, false },  // Osc2 Fine Tune
    { 34, 54, kAll, true },  // Osc2 Keyboard Tracking
    { 35, 52, 0x03, false }, // Oscillator Sync (Ring Mod/Sync button)
    { 36, 13, kAll, false }, // Amp Env Decay
    { 37, 14, kAll, false }, // Amp Env Sustain
    { 38, 8, kAll, false },  // Filter Env Attack
    { 39, 9, kAll, false },  // Filter Env Decay
    { 40, 10, kAll, false }, // Filter Env Sustain
    { 41, 11, kAll, false }, // Filter Env Release
    { 42, 4, kAll, false },  // Filter Resonance
    { 43, 5, kAll, false },  // Filter Env Amount
    { 44, 53, kAll, false }, // Filter Mode
    { 45, 63, kAll, true },  // Filter Velocity
    { 46, 55, kAll, false }, // Filter Keyboard Track
    { 65, 62, kAll, true },  // Portamento Auto
    { 70, 7, kAll, false },  // Osc FM Depth
    { 72, 15, kAll, false }, // Amp Env Release
    { 73, 12, kAll, false }, // Amp Env Attack
    { 74, 3, kAll, false },  // Filter Cutoff
    { 78, 0, kAll, false },  // Osc2 Semitones
    { 79, 6, kAll, false },  // Osc Pulse Width
    { 80, 52, 0x10, true },  // Filter Distortion
} };

const PanelCc* panelCc(std::uint8_t cc)
{
    const auto* entry = std::find_if(kPanelCcs.begin(), kPanelCcs.end(), [cc](const auto& e) { return e.cc == cc; });
    return entry == kPanelCcs.end() ? nullptr : entry;
}

int lowestBit(std::uint8_t bits)
{
    int shift = 0;
    while (((bits >> shift) & 1) == 0)
        ++shift;
    return shift;
}

// The control's value as stored in its byte.
int controlValue(const SimulatedNordLead2X::Program& program, const PanelCc& control)
{
    const auto byte = program[control.offset];
    if (control.bits == kAll)
        return byte;
    return (static_cast<std::uint8_t>(byte) & control.bits) >> lowestBit(control.bits);
}

void setControlValue(SimulatedNordLead2X::Program& program, const PanelCc& control, int value)
{
    if (control.bits == kAll) {
        program[control.offset] = static_cast<std::int8_t>(value);
        return;
    }
    const auto kept = static_cast<std::uint8_t>(program[control.offset]) & ~control.bits;
    const auto field = (value << lowestBit(control.bits)) & control.bits;
    program[control.offset] = static_cast<std::int8_t>(static_cast<std::uint8_t>(kept | field));
}

constexpr std::uint8_t kControlChange = 0xB0;

// Performance settings the synth ignores in a received dump: "not received!!!" [OFF-2X p.112],
// decoded offsets (research §3, Performance layout).
constexpr std::array<std::size_t, 8> kNotReceivedPerformanceOffsets { 315, 316, 317, 318, 319, 320, 321, 323 };

// Decoded offset of Slot A's layer flag ("channel_actived") [OFF-2X p.112].
constexpr std::size_t kLayerOffset = 324;
// Decoded offsets of Slot A's pgmselect and bankselect [OFF-2X p.112].
constexpr std::size_t kProgramNumberOffset = 328;
constexpr std::size_t kProgramBankOffset = 332;

constexpr std::uint8_t kProgramChange = 0xC0;

std::size_t settingIndex(std::size_t offset)
{
    return offset - kPerformanceSlotChannelOffset;
}

std::int8_t byte(int value)
{
    return static_cast<std::int8_t>(static_cast<std::uint8_t>(value));
}

// A test Program whose first bytes name where it comes from; the rest is a pattern.
SimulatedNordLead2X::Program testProgram(int first, int second, int third)
{
    SimulatedNordLead2X::Program program {};
    for (std::size_t i = 0; i < program.size(); ++i)
        program[i] = byte(static_cast<int>(i) * 11 + first * 37 + second * 5 + third * 3);
    program[0] = byte(first);
    program[1] = byte(second);
    program[2] = byte(third);
    return program;
}

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

    for (std::size_t bank = 0; bank < banks.size(); ++bank)
        for (std::size_t location = 0; location < banks[bank].size(); ++location)
            banks[bank][location] = testProgram(static_cast<int>(bank), static_cast<int>(location), 0);

    for (std::size_t bank = 0; bank < kits.size(); ++bank)
        for (std::size_t kit = 0; kit < kits[bank].size(); ++kit)
            for (std::size_t program = 0; program < kPercussionKitProgramCount; ++program) {
                const auto sound = testProgram(100 + static_cast<int>(bank), static_cast<int>(kit),
                                               static_cast<int>(program));
                std::copy(sound.begin(), sound.end(),
                          kits[bank][kit].begin() + static_cast<std::ptrdiff_t>(program * kProgramParameterCount));
            }

    for (std::size_t bank = 0; bank < performances.size(); ++bank)
        for (std::size_t number = 0; number < performances[bank].size(); ++number) {
            auto& performance = performances[bank][number];
            const int shownBank = static_cast<int>(bank) + 1;
            for (auto slot : kAllSlots) {
                const auto index = slotIndex(slot);
                const auto program = testProgram(50 + shownBank, static_cast<int>(number), static_cast<int>(index));
                std::copy(program.begin(), program.end(),
                          performance.begin() + static_cast<std::ptrdiff_t>(index * kProgramParameterCount));
                performance[kPerformanceSlotChannelOffset + index] = byte(static_cast<int>((index + number) % 12));
                performance[kLayerOffset + index] = slot == Slot::A ? 1 : 0;
                performance[kProgramNumberOffset + index] = byte(static_cast<int>(number % 99));
                performance[kProgramBankOffset + index] = byte(static_cast<int>(bank));
            }
        }
}

void SimulatedNordLead2X::receive(std::span<const std::uint8_t> message, TimeMs timeMs)
{
    now = timeMs;
    receive(message);
}

void SimulatedNordLead2X::receive(std::span<const std::uint8_t> message)
{
    const auto channel = message.empty() ? MidiChannel {} : static_cast<MidiChannel>(message[0] & 0x0F);
    if (message.size() == 3 && (message[0] & 0xF0) == kControlChange && message[1] == hardware::kBankSelectController
        && hardware::kBankSelectFollowsProgramChangeSetting) {
        if (programChangeOn)
            bankSelect[channel] = message[2];
    } else if (message.size() == 3 && (message[0] & 0xF0) == kControlChange && receivesCcs)
        handleControlChange(channel, message[1], message[2]);
    else if (message.size() == 2 && (message[0] & 0xF0) == kProgramChange && programChangeOn)
        handleProgramChange(channel, message[1]);
    else if (! message.empty() && message[0] == kSysExStart)
        handleSysEx(message);
}

void SimulatedNordLead2X::turnPanelControl(Slot slot, std::uint8_t cc, std::uint8_t value)
{
    const auto& state = slots[slotIndex(slot)];
    if (! state.channel)
        return;

    handleControlChange(*state.channel, cc, value);
    if (! transmitsCcs)
        return;
    const std::array<std::uint8_t, 3> message { static_cast<std::uint8_t>(kControlChange | *state.channel), cc, value };
    transmit(message);
}

void SimulatedNordLead2X::playKey(Slot slot, std::uint8_t note)
{
    const auto& state = slots[slotIndex(slot)];
    if (! state.channel)
        return;

    constexpr std::uint8_t kNoteOn = 0x90;
    constexpr std::uint8_t kNoteOff = 0x80;
    const std::array<std::uint8_t, 3> on { static_cast<std::uint8_t>(kNoteOn | *state.channel), note, 100 };
    const std::array<std::uint8_t, 3> off { static_cast<std::uint8_t>(kNoteOff | *state.channel), note, 0 };
    transmit(on);
    transmit(off);
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
    const auto* control = panelCc(cc);
    if (control == nullptr)
        return;
    const auto stored = hardware::valueFromCcData(cc, control->isSwitch, value);
    if (! stored)
        return;

    for (auto slot : kAllSlots)
        if (receivesOn(slot, channel))
            setControlValue(slots[slotIndex(slot)].editBuffer, *control, *stored);
}

bool SimulatedNordLead2X::layered(Slot slot) const
{
    return settings[kLayerOffset - kPerformanceSlotChannelOffset + slotIndex(slot)] != 0;
}

bool SimulatedNordLead2X::receivesOn(Slot slot, MidiChannel channel) const
{
    // Every Slot on the channel reacts [OFF-2X p.77].
    if (slots[slotIndex(slot)].channel == channel)
        return true;
    // Slots in a Layer also receive on the leftmost active Slot's channel [OFF-2X p.76–77].
    if (! hardware::kLayeredSlotsAlsoReceiveOnLeftmostActiveSlotChannel || ! layered(slot))
        return false;
    for (auto other : kAllSlots)
        if (layered(other))
            return slots[slotIndex(other)].channel == channel;
    return false;
}

void SimulatedNordLead2X::setLayered(Slot slot, bool inLayer)
{
    settings[kLayerOffset - kPerformanceSlotChannelOffset + slotIndex(slot)] = inLayer ? 1 : 0;
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
        if (answersPerformanceRequests)
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

int SimulatedNordLead2X::receivedDumpCount(DumpDestination destination) const
{
    return static_cast<int>(std::count_if(dumpLog.begin(), dumpLog.end(),
                                          [destination](const auto& dump) { return dump.destination == destination; }));
}

void SimulatedNordLead2X::handleDump(const ParsedSysEx& dump)
{
    const auto& values = *dump.values;

    const bool toEditBuffer = dump.kind == SysExKind::PerformanceDump
                                  ? dump.type == hardware::kPerformanceEditBufferDumpType
                                  : dump.type == hardware::kProgramEditBufferDumpType;
    dumpLog.push_back({ dump.kind, toEditBuffer ? DumpDestination::EditBuffer : DumpDestination::Bank, dump.type,
                        dump.spec });

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
        ++receivedPerformanceDumps;
        applyPerformance(values);
    }
}

void SimulatedNordLead2X::applyPerformance(std::span<const std::int8_t> values)
{
    for (auto slot : kAllSlots) {
        // A Kit is stored in a Performance as a reference only [OFF-2X p.31–32]: a Slot playing
        // one keeps it.
        if (playsPercussionKit(slot))
            continue;
        const auto first = values.begin() + static_cast<std::ptrdiff_t>(slotIndex(slot) * kProgramParameterCount);
        std::copy(first, first + static_cast<std::ptrdiff_t>(kProgramParameterCount),
                  slots[slotIndex(slot)].editBuffer.begin());
    }
    for (std::size_t offset = kPerformanceSlotChannelOffset; offset < kPerformanceParameterCount; ++offset) {
        const bool notReceived = std::find(kNotReceivedPerformanceOffsets.begin(), kNotReceivedPerformanceOffsets.end(),
                                           offset)
                                 != kNotReceivedPerformanceOffsets.end();
        if (! notReceived)
            settings[settingIndex(offset)] = values[offset];
    }
    for (auto slot : kAllSlots)
        slots[slotIndex(slot)].channel = hardware::slotChannelFromPerformance(settings[slotIndex(slot)]);
}

const SimulatedNordLead2X::PercussionKit& SimulatedNordLead2X::storedPercussionKit(int shownBank, int kit) const
{
    return kits.at(static_cast<std::size_t>(shownBank)).at(static_cast<std::size_t>(kit));
}

const SimulatedNordLead2X::Performance& SimulatedNordLead2X::storedPerformance(int bank, int number) const
{
    return performances.at(static_cast<std::size_t>(bank - 1)).at(static_cast<std::size_t>(number));
}

void SimulatedNordLead2X::setStoredPerformance(int bank, int number, const Performance& performance)
{
    performances.at(static_cast<std::size_t>(bank - 1)).at(static_cast<std::size_t>(number)) = performance;
}

void SimulatedNordLead2X::handleProgramChange(MidiChannel channel, std::uint8_t number)
{
    // The Global Channel selects Performances, and wins over a Slot on it [OFF-2X p.76].
    if (channel == globalChannel) {
        int bank = performanceBank;
        if (bankSelect[channel])
            bank = *bankSelect[channel] - hardware::kBankSelectOfPerformanceBank1 + 1;
        if (bank < 1 || bank > hardware::kPerformanceBankCount || number >= hardware::kPerformancesPerBank)
            return;
        ++receivedProgramChanges;
        loadStoredPerformance(bank, number);
        return;
    }

    std::array<bool, 4> reached {};
    for (auto slot : kAllSlots)
        reached[slotIndex(slot)] = receivesOn(slot, channel);
    if (std::find(reached.begin(), reached.end(), true) == reached.end())
        return;
    ++receivedProgramChanges;
    for (auto slot : kAllSlots) {
        if (! reached[slotIndex(slot)])
            continue;
        int bank = settings[settingIndex(kProgramBankOffset) + slotIndex(slot)];
        if (bankSelect[channel])
            bank = *bankSelect[channel] - hardware::kBankSelectOfProgramBank0;
        loadStoredProgram(slot, bank, number - hardware::kProgramChangeOfProgram1);
    }
}

void SimulatedNordLead2X::loadStoredProgram(Slot slot, int shownBank, int location)
{
    auto& state = slots[slotIndex(slot)];
    const int kit = location + hardware::kProgramChangeOfProgram1 - hardware::kProgramChangeOfPercussionKitP0;
    if (kit >= 0) {
        if (shownBank < 0 || shownBank >= hardware::kPercussionKitBankCount || kit >= hardware::kPercussionKitsPerBank)
            return;
        state.percussionKit = storedPercussionKit(shownBank, kit);
    } else {
        if (shownBank < 0 || shownBank >= kProgramBankCount || location < 0 || location >= kProgramsPerBank)
            return;
        state.editBuffer = bankProgram(shownBank, location);
        state.percussionKit.reset();
    }
    // The Performance Edit Buffer remembers where the Slot was loaded from [OFF-2X p.112], [INF].
    settings[settingIndex(kProgramNumberOffset) + slotIndex(slot)] = byte(location + hardware::kProgramChangeOfProgram1);
    settings[settingIndex(kProgramBankOffset) + slotIndex(slot)] = byte(shownBank);
}

void SimulatedNordLead2X::loadStoredPerformance(int bank, int number)
{
    performanceBank = bank;
    // A newly selected Performance plays its own Programs; Kit references are not simulated.
    for (auto& state : slots)
        state.percussionKit.reset();
    applyPerformance(storedPerformance(bank, number));
}

void SimulatedNordLead2X::selectProgramOnPanel(Slot slot, int shownBank, int location)
{
    const auto channel = slots[slotIndex(slot)].channel;
    if (! channel)
        return;
    // The panel changes the active Slot only.
    loadStoredProgram(slot, shownBank, location);
    if (! programChangeOn)
        return;
    const std::array<std::uint8_t, 3> bankMessage {
        static_cast<std::uint8_t>(kControlChange | *channel), hardware::kBankSelectController,
        static_cast<std::uint8_t>(shownBank + hardware::kBankSelectOfProgramBank0)
    };
    const std::array<std::uint8_t, 2> programMessage {
        static_cast<std::uint8_t>(kProgramChange | *channel),
        static_cast<std::uint8_t>(location + hardware::kProgramChangeOfProgram1)
    };
    transmit(bankMessage);
    transmit(programMessage);
}

void SimulatedNordLead2X::selectPerformanceOnPanel(int bank, int number)
{
    loadStoredPerformance(bank, number);
    if (! programChangeOn)
        return;
    const std::array<std::uint8_t, 3> bankMessage {
        static_cast<std::uint8_t>(kControlChange | globalChannel), hardware::kBankSelectController,
        static_cast<std::uint8_t>(bank - 1 + hardware::kBankSelectOfPerformanceBank1)
    };
    const std::array<std::uint8_t, 2> programMessage { static_cast<std::uint8_t>(kProgramChange | globalChannel),
                                                       static_cast<std::uint8_t>(number) };
    transmit(bankMessage);
    transmit(programMessage);
}

void SimulatedNordLead2X::sendAllControllers(Slot slot)
{
    const auto& state = slots[slotIndex(slot)];
    if (! state.channel || (hardware::kAllControllersReplyNeedsControlChangeOn && ! transmitsCcs))
        return;

    for (const auto& control : kPanelCcs) {
        const auto data = hardware::ccDataFromValue(control.cc, control.isSwitch, controlValue(state.editBuffer, control));
        const std::array<std::uint8_t, 3> message { static_cast<std::uint8_t>(kControlChange | *state.channel),
                                                    control.cc, data };
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
