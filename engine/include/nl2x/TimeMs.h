#pragma once

#include <cstdint>

namespace nl2x {

// Milliseconds on a monotonic clock owned by the caller. The engine never reads a clock.
using TimeMs = std::int64_t;

} // namespace nl2x
