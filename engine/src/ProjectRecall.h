#pragma once

#include "nl2x/Slot.h"

#include <array>
#include <cstdint>
#include <deque>
#include <vector>

namespace nl2x {

// What the engine remembers about sending a project's sounds to the synth (#8). Private to the
// engine; Engine (EngineRecall.cpp) builds the dumps and sends them when the connection is free.
struct ProjectRecall {
    // A project was loaded with automatic sending on before the Global Channel was known: the
    // Synth model's sounds go to the synth as soon as it is found, instead of reading the Slots.
    bool held = false;
    // Connected, but the memory writer was busy (#12): the recall starts once it is idle.
    bool waitingForMemoryWriter = false;
    // Edit Buffer dumps (type 30, then type 0) still to send, in order.
    std::deque<std::vector<std::uint8_t>> dumps;
    // What the project could not say and is read from the synth once the dumps are out: Slots
    // playing a Kit or unknown, and the Performance when the project had none.
    std::array<bool, 4> readSlotsAfter {};
    bool readPerformanceAfter = false;
};

} // namespace nl2x
