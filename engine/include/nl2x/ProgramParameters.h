#pragma once

#include "nl2x/Slot.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace nl2x {

// A Program holds 66 parameters [OFF-2X p.111–112].
inline constexpr std::size_t kProgramParameterCount = 66;

// The Program parameters the editor knows. Grows to all 66 (#6, #7).
enum class ProgramParameter : std::uint8_t {
    FilterCutoff,
};

struct ProgramParameterInfo {
    ProgramParameter id;
    // Never changes once released: DAW automation and saved projects refer to it.
    std::string_view key;
    // The manual's term, shown to the user.
    std::string_view name;
    // Panel CC [OFF-2X p.103–104], or none for parameters reachable only by SysEx.
    std::optional<std::uint8_t> cc;
    // Position in the decoded Program dump [OFF-2X p.111–112]. Pairing it with the CC is [INF]
    // (research §2): the names match one to one.
    std::uint8_t sysExOffset;
    int minValue;
    int maxValue;
    // Named steps for stepped parameters, lowest first; empty for continuous ones.
    std::span<const std::string_view> choices;
};

std::span<const ProgramParameterInfo> programParameters();
const ProgramParameterInfo& programParameterInfo(ProgramParameter parameter);

// DAW-facing identity of a Slot's parameter: a stable ID ("A_filterCutoff") and the name the
// DAW shows ("A: Filter Cutoff").
std::string dawParameterId(Slot slot, ProgramParameter parameter);
std::string dawParameterName(Slot slot, ProgramParameter parameter);

} // namespace nl2x
