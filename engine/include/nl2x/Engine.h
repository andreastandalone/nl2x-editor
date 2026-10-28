#pragma once

#include "nl2x/MidiOutput.h"
#include "nl2x/ProgramParameters.h"
#include "nl2x/Slot.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace nl2x {

// Milliseconds on a monotonic clock owned by the caller. The engine never reads a clock.
using TimeMs = std::int64_t;

// Everything the editor does, behind one seam: MIDI messages in, MIDI messages out.
// Single-threaded: the caller invokes it from one thread only (CODING_STANDARDS.md).
class Engine {
public:
    explicit Engine(MidiOutput& output);

    // A complete MIDI message received from the synth.
    void handleIncomingMidi(std::span<const std::uint8_t> message, TimeMs timeMs);

    // Called regularly by the caller; drives throttling, timeouts and pacing.
    void advanceTime(TimeMs timeMs);

    // The user (GUI or DAW automation) set a parameter of a Slot's Program, in the parameter's
    // own range (ProgramParameterInfo::minValue … maxValue).
    void setParameter(Slot slot, ProgramParameter parameter, int value);

private:
    MidiOutput& output;
    // Empty when a Slot's channel is off.
    std::array<std::optional<MidiChannel>, 4> slotChannels;
};

} // namespace nl2x
