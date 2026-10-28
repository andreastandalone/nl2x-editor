#pragma once

#include "nl2x/Engine.h"
#include "nl2x/HardwareAssumptions.h"
#include "nl2x/MidiOutput.h"
#include "nl2x/ProgramParameters.h"
#include "nl2x/Slot.h"
#include "nl2x/SysEx.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace nl2x::test {

// A test double for the synth, behaving as the real Nord Lead 2X does according to
// docs/research/nl2x-midi-research.md and HardwareAssumptions.h. It deliberately does not use the
// engine's parameter table: it is an independent reading of the manual, so a mistake in the
// engine shows up as a disagreement.
//
// It understands every panel CC of the CC chart on Slot Channels (with the value conventions of
// HardwareAssumptions.h), the Slot Edit Buffer request, the All Controllers Request,
// Program dumps into Edit Buffers and Bank locations, the Performance Edit Buffer request and
// dump (ignoring the settings the synth does not receive), Slots listening on their Slot Channels
// and, when layered, on the leftmost active Slot's channel, and Slots playing a Percussion Kit
// (a Kit Edit Buffer dump reaches only a Slot playing a Kit, a Program only one playing a Program).
// It records every dump it receives by destination, so tests can prove nothing writes to Banks.
// It holds Program Banks 0–9 with Percussion Kits P0–P9 in Banks 0–3 and Performance Banks 1–4,
// filled with distinct test contents, and selects them on Bank Select (CC32) and Program Change:
// on a Slot Channel into the Slots receiving there, on the Global Channel as a Performance.
// Replies are sent at once; the misbehaviour switches below model a synth that does not answer or
// cannot keep up.
class SimulatedNordLead2X {
public:
    using Program = std::array<std::int8_t, kProgramParameterCount>;
    using PercussionKit = std::array<std::int8_t, kPercussionKitParameterCount>;
    using PerformanceSettings = std::array<std::int8_t, kPerformanceSettingsCount>;

    using Performance = std::array<std::int8_t, kPerformanceParameterCount>;

    // The four RAM Program Banks, numbered as the synth shows them (0–3) [OFF-2X p.13].
    static constexpr int kRamBankCount = 4;
    static constexpr int kProgramsPerBank = 99;
    // All Program Banks, 0–9 (4–9 are ROM) [OFF-2X p.13].
    static constexpr int kProgramBankCount = 10;

    SimulatedNordLead2X();
    // A synth following other readings of the unverified hardware switches (HardwareAssumptions.h).
    explicit SimulatedNordLead2X(hardware::Switches switches);

    // A complete message arriving at the synth's MIDI In, at `timeMs` (used only by the pacing
    // misbehaviour); without a time, the time of the previous message is kept.
    void receive(std::span<const std::uint8_t> message);
    void receive(std::span<const std::uint8_t> message, TimeMs timeMs);

    // The cable plugged into the synth's MIDI Out; nullptr when none is (misbehaviour "MIDI
    // return cable missing": everything the synth sends goes nowhere).
    void connectMidiOut(MidiOutput* output) { midiOut = output; }

    // The user plays a key while `slot` is the active Slot: Note On and Note Off go out on that
    // Slot's channel, whatever the C setting says (it covers CCs only [OFF-2X p.66]).
    void playKey(Slot slot, std::uint8_t note);

    // The user turns a panel control on the synth while `slot` is the active Slot: the Edit
    // Buffer changes and the CC goes out on that Slot's channel [OFF-2X p.76].
    void turnPanelControl(Slot slot, std::uint8_t cc, std::uint8_t value);

    // A Slot's Program Edit Buffer, by decoded SysEx offset [OFF-2X p.111–112]. Setting it makes
    // the Slot play a Program.
    const Program& editBuffer(Slot slot) const { return slots[slotIndex(slot)].editBuffer; }
    void setEditBuffer(Slot slot, const Program& program);

    // The Slot plays a Percussion Kit (8 Programs, by decoded offset) until setEditBuffer().
    void playPercussionKit(Slot slot, const PercussionKit& kit) { slots[slotIndex(slot)].percussionKit = kit; }
    bool playsPercussionKit(Slot slot) const { return slots[slotIndex(slot)].percussionKit.has_value(); }

    // The Slot selected on the panel. It matters only if Slot Edit Buffer requests follow the
    // active Slot (kSlotEditBufferReplyFollowsRequestedSlot in HardwareAssumptions.h).
    void setActiveSlot(Slot slot) { activeSlot = slot; }

    void setSlotChannel(Slot slot, std::optional<MidiChannel> channel) { slots[slotIndex(slot)].channel = channel; }
    void setGlobalChannel(GlobalChannel channel) { globalChannel = channel; }

    // The Performance settings (decoded offsets 264–353, here 0–89); Slot Channels are kept in
    // the Slots and written into offsets 264–267 when a Performance is dumped.
    const PerformanceSettings& performanceSettings() const { return settings; }
    void setPerformanceSettings(const PerformanceSettings& newSettings) { settings = newSettings; }
    // The Slot's layer flag (offset 324 + Slot): activated Slots play together.
    void setLayered(Slot slot, bool inLayer);
    std::optional<MidiChannel> slotChannel(Slot slot) const { return slots[slotIndex(slot)].channel; }
    // How many Performance dumps to the Edit Buffer arrived.
    int receivedPerformanceDumpCount() const { return receivedPerformanceDumps; }

    // Every dump that arrived (and was not lost to a misbehaviour), by destination as its type byte
    // names it: an Edit Buffer (Program/Kit type 0, Performance type 30) or a Bank (any other
    // type), whether or not the synth applied it. Tests assert on it that nothing writes to Banks.
    enum class DumpDestination : std::uint8_t { EditBuffer, Bank };
    struct ReceivedDump {
        SysExKind kind;
        DumpDestination destination;
        std::uint8_t type;
        std::uint8_t spec;
        // When it arrived, so that tests can check the rate of dumps.
        TimeMs timeMs;
    };
    const std::vector<ReceivedDump>& receivedDumps() const { return dumpLog; }
    // When each Program dump to `slot`'s Edit Buffer (type 0, spec = Slot) arrived, in order.
    std::vector<TimeMs> programEditBufferDumpTimes(Slot slot) const;
    int receivedDumpCount(DumpDestination destination) const;

    // The Percussion Kit a Slot plays (8 Programs, by decoded offset); empty while it plays a Program.
    const std::optional<PercussionKit>& percussionKit(Slot slot) const { return slots[slotIndex(slot)].percussionKit; }

    // Bank locations, numbered as the synth shows Banks (0–9) and as Programs are sent (0–98).
    // Every location starts with a distinct test Program (byte 0 = Bank, byte 1 = location).
    const Program& bankProgram(int shownBank, int location) const;
    void setBankProgram(int shownBank, int location, const Program& program);
    // The Bank selected on the synth's panel: Bank dumps are stored there [OFF-2X p.78].
    void selectBank(int shownBank) { selectedBank = shownBank; }

    // Stored Percussion Kits P0–P9 (0–9) of Banks 0–3; each starts distinct (Program 0's byte 0 =
    // 100 + Bank, byte 1 = Kit).
    const PercussionKit& storedPercussionKit(int shownBank, int kit) const;
    // Stored Performances, Bank 1–4, number 0–99 (A0–L9). Each starts distinct: Slot X's Program has
    // byte 0 = 50 + Bank, byte 1 = number, byte 2 = Slot; Slot Channels (Slot + number) mod 12;
    // Slot A alone in the Layer; pgmselect = number mod 99 and bankselect = Bank − 1 for all Slots.
    const Performance& storedPerformance(int bank, int number) const;
    void setStoredPerformance(int bank, int number, const Performance& performance);

    // The P setting under Shift + PROG/CTRL: Program Change (and, by HardwareAssumptions.h, Bank
    // Select) sent and received [OFF-2X p.66–67]. On by default.
    void setProgramChangeOn(bool on) { programChangeOn = on; }
    // How many Program Changes arrived and were acted on.
    int receivedProgramChangeCount() const { return receivedProgramChanges; }

    // The user selects a stored Program (location 0–98) or Kit (99–108) on the panel while `slot` is
    // active: that Slot loads it, and with P on CC32 and Program Change go out on that
    // channel [OFF-2X p.76].
    void selectProgramOnPanel(Slot slot, int shownBank, int location);
    // The user selects a stored Performance on the panel: it is loaded, and with P on CC32 and
    // Program Change go out on the Global Channel (HardwareAssumptions.h, [INF]).
    void selectPerformanceOnPanel(int bank, int number);

    // The request type that reads SysEx Bank 1; Banks 2–4 follow. Unknown on a 2X
    // (HardwareAssumptions.h), so by default the simulated synth answers no Bank request; tests
    // that need Bank reads choose a type.
    void setBankRequestTypeOfSysExBank1(std::optional<std::uint8_t> type) { bankRequestType = type; }

    // The C setting under Shift + PROG/CTRL, which switches CC transmission and reception
    // together [OFF-2X p.66–67]. On by default.
    void setControlChangeOn(bool on) { transmitsCcs = receivesCcs = on; }
    // Misbehaviour, for testing each half alone: no CC is transmitted (panel knobs, and the All
    // Controllers Request when HardwareAssumptions.h says C gates it).
    void setTransmitsControlChanges(bool transmits) { transmitsCcs = transmits; }
    // Misbehaviour: incoming CCs are ignored.
    void setReceivesControlChanges(bool receives) { receivesCcs = receives; }

    // Misbehaviour: ignore every SysEx message.
    void setAnswersSysEx(bool answers) { answersSysEx = answers; }
    // Misbehaviour: ignore the Performance Edit Buffer request (type 40 is unverified, research §7.3).
    void setAnswersPerformanceRequests(bool answers) { answersPerformanceRequests = answers; }
    // Misbehaviour: a SysEx message arriving less than `recoveryMs` after the previous one is lost,
    // as if the synth were still digesting it.
    void setSysExRecoveryMs(TimeMs recoveryMs) { sysExRecoveryMs = recoveryMs; }

    // How many SysEx messages were lost to the pacing misbehaviour.
    int lostSysExCount() const { return lostSysEx; }

private:
    struct SlotState {
        Program editBuffer {};
        // Set while the Slot plays a Percussion Kit instead of its Program.
        std::optional<PercussionKit> percussionKit;
        std::optional<MidiChannel> channel;
    };

    void handleControlChange(MidiChannel channel, std::uint8_t cc, std::uint8_t value);
    void handleProgramChange(MidiChannel channel, std::uint8_t number);
    void loadStoredProgram(Slot slot, int shownBank, int location);
    void loadStoredPerformance(int bank, int number);
    // Copies a Performance's Programs into the Slots and its settings, but those the synth does not
    // receive, into the synth's.
    void applyPerformance(std::span<const std::int8_t> values);
    bool layered(Slot slot) const;
    bool receivesOn(Slot slot, MidiChannel channel) const;
    void handleSysEx(std::span<const std::uint8_t> message);
    void handleRequest(std::uint8_t type, std::uint8_t spec);
    void sendSlotEditBuffer(Slot requested);
    void handleDump(const ParsedSysEx& dump);
    void sendAllControllers(Slot slot);
    void sendPerformance();
    void transmit(std::span<const std::uint8_t> message);

    hardware::Switches switches;
    std::array<SlotState, 4> slots;
    PerformanceSettings settings {};
    std::array<std::array<Program, kProgramsPerBank>, kProgramBankCount> banks {};
    std::array<std::array<PercussionKit, hardware::kPercussionKitsPerBank>, hardware::kPercussionKitBankCount> kits {};
    std::array<std::array<Performance, hardware::kPerformancesPerBank>, hardware::kPerformanceBankCount> performances {};
    // The last Bank Select received on each channel.
    std::array<std::optional<std::uint8_t>, 16> bankSelect;
    int performanceBank = 1;
    bool programChangeOn = true;
    int receivedProgramChanges = 0;
    int selectedBank = 0;
    Slot activeSlot = Slot::A;
    GlobalChannel globalChannel;
    std::optional<std::uint8_t> bankRequestType;
    bool answersSysEx = true;
    bool answersPerformanceRequests = true;
    int receivedPerformanceDumps = 0;
    std::vector<ReceivedDump> dumpLog;
    bool transmitsCcs = true;
    bool receivesCcs = true;
    TimeMs sysExRecoveryMs = 0;
    TimeMs now = 0;
    std::optional<TimeMs> lastSysExTime;
    int lostSysEx = 0;
    MidiOutput* midiOut = nullptr;
};

} // namespace nl2x::test
