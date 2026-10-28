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

namespace nl2x::test {

// A test double for the synth, behaving as the real Nord Lead 2X does according to
// docs/research/nl2x-midi-research.md and HardwareAssumptions.h. It deliberately does not use the
// engine's parameter table: it is an independent reading of the manual, so a mistake in the
// engine shows up as a disagreement.
//
// It understands CCs on Slot Channels, the Slot Edit Buffer request, the All Controllers Request,
// Program dumps into Edit Buffers and Bank locations, and the Performance Edit Buffer request and
// dump. Percussion Kits, Performance Banks and Slot Channel changes from Program Change (#5) are
// added as those tickets land. Replies are sent at once; the misbehaviour switches below model a
// synth that does not answer or cannot keep up.
class SimulatedNordLead2X {
public:
    using Program = std::array<std::int8_t, kProgramParameterCount>;
    using PerformanceSettings = std::array<std::int8_t, kPerformanceSettingsCount>;

    // The four RAM Program Banks, numbered as the synth shows them (0–3) [OFF-2X p.13].
    static constexpr int kRamBankCount = 4;
    static constexpr int kProgramsPerBank = 99;

    SimulatedNordLead2X();

    // A complete message arriving at the synth's MIDI In, at `timeMs` (used only by the pacing
    // misbehaviour); without a time, the time of the previous message is kept.
    void receive(std::span<const std::uint8_t> message);
    void receive(std::span<const std::uint8_t> message, TimeMs timeMs);

    // The cable plugged into the synth's MIDI Out; nullptr when none is.
    void connectMidiOut(MidiOutput* output) { midiOut = output; }

    // The user turns a panel control on the synth while `slot` is the active Slot: the Edit
    // Buffer changes and the CC goes out on that Slot's channel [OFF-2X p.76].
    void turnPanelControl(Slot slot, std::uint8_t cc, std::uint8_t value);

    // A Slot's Edit Buffer, by decoded SysEx offset [OFF-2X p.111–112].
    const Program& editBuffer(Slot slot) const { return slots[slotIndex(slot)].editBuffer; }
    void setEditBuffer(Slot slot, const Program& program) { slots[slotIndex(slot)].editBuffer = program; }

    void setSlotChannel(Slot slot, std::optional<MidiChannel> channel) { slots[slotIndex(slot)].channel = channel; }
    void setGlobalChannel(GlobalChannel channel) { globalChannel = channel; }

    // The Performance settings (decoded offsets 264–353, here 0–89); Slot Channels are kept in
    // the Slots and written into offsets 264–267 when a Performance is dumped.
    const PerformanceSettings& performanceSettings() const { return settings; }
    void setPerformanceSettings(const PerformanceSettings& newSettings) { settings = newSettings; }

    // Bank locations, numbered as the synth shows Banks (0–3) and as Programs are sent (0–98).
    const Program& bankProgram(int shownBank, int location) const;
    void setBankProgram(int shownBank, int location, const Program& program);
    // The Bank selected on the synth's panel: Bank dumps are stored there [OFF-2X p.78].
    void selectBank(int shownBank) { selectedBank = shownBank; }

    // The request type that reads SysEx Bank 1; Banks 2–4 follow. Unknown on a 2X
    // (HardwareAssumptions.h), so by default the simulated synth answers no Bank request; tests
    // that need Bank reads choose a type.
    void setBankRequestTypeOfSysExBank1(std::optional<std::uint8_t> type) { bankRequestType = type; }

    // Misbehaviour: ignore every SysEx message.
    void setAnswersSysEx(bool answers) { answersSysEx = answers; }
    // Misbehaviour: a SysEx message arriving less than `recoveryMs` after the previous one is lost,
    // as if the synth were still digesting it.
    void setSysExRecoveryMs(TimeMs recoveryMs) { sysExRecoveryMs = recoveryMs; }

    // How many SysEx messages were lost to the pacing misbehaviour.
    int lostSysExCount() const { return lostSysEx; }

private:
    struct SlotState {
        Program editBuffer {};
        std::optional<MidiChannel> channel;
    };

    void handleControlChange(MidiChannel channel, std::uint8_t cc, std::uint8_t value);
    void handleSysEx(std::span<const std::uint8_t> message);
    void handleRequest(std::uint8_t type, std::uint8_t spec);
    void handleDump(const ParsedSysEx& dump);
    void sendAllControllers(Slot slot);
    void sendPerformance();
    void transmit(std::span<const std::uint8_t> message);

    std::array<SlotState, 4> slots;
    PerformanceSettings settings {};
    std::array<std::array<Program, kProgramsPerBank>, kRamBankCount> banks {};
    int selectedBank = 0;
    GlobalChannel globalChannel;
    std::optional<std::uint8_t> bankRequestType;
    bool answersSysEx = true;
    TimeMs sysExRecoveryMs = 0;
    TimeMs now = 0;
    std::optional<TimeMs> lastSysExTime;
    int lostSysEx = 0;
    MidiOutput* midiOut = nullptr;
};

} // namespace nl2x::test
