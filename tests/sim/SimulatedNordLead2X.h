#pragma once

#include "nl2x/MidiOutput.h"
#include "nl2x/ProgramParameters.h"
#include "nl2x/Slot.h"

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
// Today it understands CCs on Slot Channels. SysEx requests and dumps (#4, #7), Performances and
// Slot Channel changes (#5), Banks (#11, #12) and misbehaviour switches for the Connection check
// (#9) are added here as those tickets land.
class SimulatedNordLead2X {
public:
    using Program = std::array<std::int8_t, kProgramParameterCount>;

    SimulatedNordLead2X();

    // A complete message arriving at the synth's MIDI In.
    void receive(std::span<const std::uint8_t> message);

    // The cable plugged into the synth's MIDI Out; nullptr when none is.
    void connectMidiOut(MidiOutput* output) { midiOut = output; }

    // The user turns a panel control on the synth while `slot` is the active Slot: the Edit
    // Buffer changes and the CC goes out on that Slot's channel [OFF-2X p.76].
    void turnPanelControl(Slot slot, std::uint8_t cc, std::uint8_t value);

    // A Slot's Edit Buffer, by decoded SysEx offset [OFF-2X p.111–112].
    const Program& editBuffer(Slot slot) const { return slots[slotIndex(slot)].editBuffer; }

    void setSlotChannel(Slot slot, std::optional<MidiChannel> channel) { slots[slotIndex(slot)].channel = channel; }

private:
    struct SlotState {
        Program editBuffer {};
        std::optional<MidiChannel> channel;
    };

    void handleControlChange(MidiChannel channel, std::uint8_t cc, std::uint8_t value);
    void transmit(std::span<const std::uint8_t> message);

    std::array<SlotState, 4> slots;
    MidiOutput* midiOut = nullptr;
};

} // namespace nl2x::test
