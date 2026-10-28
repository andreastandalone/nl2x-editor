#pragma once

#include <cstdint>

namespace nl2x {

// Names one undo step (#14): one knob drag, or one menu choice. Every user edit made with the same
// id joins one step, so the shell can hand a drag's values to the engine one at a time.
using EditStepId = std::uint64_t;

} // namespace nl2x
