#pragma once

#include "nl2x/Selection.h"
#include "nl2x/Slot.h"
#include "nl2x/TimeMs.h"

#include <array>
#include <cstdint>
#include <optional>

namespace nl2x {

// What the engine remembers about selections (#10): what is selected, the last Bank Select seen
// on each channel, and the re-read waiting for the synth to settle. Private to the engine;
// Engine (EngineSelection.cpp) sends the MIDI and queues the reads.
struct SelectionTracker {
    SelectionState state;
    // The last CC32 value sent or received on each MIDI channel: Bank Select holds until the next
    // one, so a Program Change arriving alone uses it.
    std::array<std::optional<std::uint8_t>, 16> bankSelect;

    // Edit Buffers to read again once the synth has settled; `due` is empty when none waits.
    std::optional<TimeMs> due;
    std::array<bool, 4> rereadSlots {};
    bool rereadPerformance = false;

    // Pushes the re-read to kSelectionSettleMs after `timeMs`, so that a burst ends in one re-read.
    void scheduleReread(TimeMs timeMs, const std::array<bool, 4>& slots, bool performance);
};

} // namespace nl2x
