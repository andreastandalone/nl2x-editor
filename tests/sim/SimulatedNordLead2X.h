#pragma once

#include "nl2x/Engine.h"
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
// It understands CCs on Slot Channels, the Slot Edit Buffer request, the All Controllers Request,
// Program dumps into Edit Buffers and Bank locations, the Performance Edit Buffer request and
// dump (ignoring the settings the synth does not receive), Slots listening on their Slot Channels
// and, when layered, on the leftmost active Slot's channel, and Slots playing a Percussion Kit.
// Performance Banks and Performance changes by Program Change are added as those tickets land.
// Replies are sent at once; the misbehaviour switches below model a synth that does not answer or
// cannot keep up.
class SimulatedNordLead2X {
public:
    using Program = std::array<std::int8_t, kProgramParameterCount>;
    using PercussionKit = std::array<std::int8_t, kPercussionKitParameterCount>;
    using PerformanceSettings = std::array<std::int8_t, kPerformanceSettingsCount>;

    // The four RAM Program Banks, numbered as the synth shows them (0–3) [OFF-2X p.13].
    static constexpr int kRamBankCount = 4;
    static constexpr int kProgramsPerBank = 99;

    SimulatedNordLead2X();

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
    };
    const std::vector<ReceivedDump>& receivedDumps() const { return dumpLog; }
    int receivedDumpCount(DumpDestination destination) const;

    // The Percussion Kit a Slot plays (8 Programs, by decoded offset); empty while it plays a Program.
    const std::optional<PercussionKit>& percussionKit(Slot slot) const { return slots[slotIndex(slot)].percussionKit; }

    // Bank locations, numbered as the synth shows Banks (0–3) and as Programs are sent (0–98).
    const Program& bankProgram(int shownBank, int location) const;
    void setBankProgram(int shownBank, int location, const Program& program);
    // The Bank selected on the synth's panel: Bank dumps are stored there [OFF-2X p.78].
    void selectBank(int shownBank) { selectedBank = shownBank; }

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
    bool layered(Slot slot) const;
    bool receivesOn(Slot slot, MidiChannel channel) const;
    void handleSysEx(std::span<const std::uint8_t> message);
    void handleRequest(std::uint8_t type, std::uint8_t spec);
    void sendSlotEditBuffer(Slot requested);
    void handleDump(const ParsedSysEx& dump);
    void sendAllControllers(Slot slot);
    void sendPerformance();
    void transmit(std::span<const std::uint8_t> message);

    std::array<SlotState, 4> slots;
    PerformanceSettings settings {};
    std::array<std::array<Program, kProgramsPerBank>, kRamBankCount> banks {};
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
