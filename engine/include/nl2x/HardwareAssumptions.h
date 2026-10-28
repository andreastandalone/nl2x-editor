#pragma once

// Every guess about the real synth's behaviour lives here, and nowhere else (CODING_STANDARDS.md).
// Each entry names the research fact it rests on (docs/research/nl2x-midi-research.md) and its
// verification status. Hardware discovery (#3) changes this file; the engine and the simulated
// synth both follow it.

#include "nl2x/Slot.h"

#include <array>

namespace nl2x::hardware {

// Slot Channels assumed until the Performance has been read from the synth (#5).
// Facts: each Slot has its own MIDI channel, 1–16 or off, stored in the Performance
// [OFF-2X p.67, 75–76]. The manual does not state a default. In the 100 factory Performances
// [OFF-FB] Slot A is always on channel 1; B, C and D vary (most often 1, 3, 4).
// Choice: channels 1, 2, 3, 4, so that the four Slots can be addressed independently.
// Status: [INF], not verified on hardware.
inline constexpr std::array<MidiChannel, 4> kDefaultSlotChannels { 0, 1, 2, 3 };

} // namespace nl2x::hardware
