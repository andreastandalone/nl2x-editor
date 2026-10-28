#pragma once

// A Performance as a value, and its conversion to and from a SysEx dump (research §3,
// "Performance layout"). As with Programs, every byte is kept exactly as received, so reading a
// dump and writing it back gives the same bytes.

#include "nl2x/PerformanceSettings.h"
#include "nl2x/Program.h"
#include "nl2x/Slot.h"
#include "nl2x/SysEx.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace nl2x {

// The four Programs (Slots A–D, 66 bytes each) and the 90 Performance settings, by decoded
// offset [OFF-2X p.110, 112]. A Slot playing a Percussion Kit holds only a reference to the Kit
// [OFF-2X p.31–32]; its 66 bytes are kept as they are.
class Performance {
public:
    using Values = std::array<std::int8_t, kPerformanceParameterCount>;

    // All zero: not a meaningful Performance, only a starting point.
    Performance() = default;
    explicit Performance(const Values& performanceValues) : values(performanceValues) {}
    // Empty unless `performanceValues` holds exactly kPerformanceParameterCount values.
    static std::optional<Performance> fromValues(std::span<const std::int8_t> performanceValues);

    Program program(Slot slot) const;
    void setProgram(Slot slot, const Program& program);

    // A setting's value: the signed byte, or the unsigned nibble for Out Mode. `slot` is ignored
    // for settings that are not per Slot.
    int setting(PerformanceSetting setting, Slot slot = Slot::A) const;
    // Stores `newValue` as the setting's byte (the caller keeps it within −128…127) or nibble
    // (0–15), leaving every other bit unchanged.
    void setSetting(PerformanceSetting setting, Slot slot, int newValue);

    const Values& bytes() const { return values; }

    friend bool operator==(const Performance&, const Performance&) = default;

private:
    Values values {};
};

// A Performance dump as it travels; the header bytes are kept so the dump is written back unchanged.
struct PerformanceDump {
    GlobalChannel globalChannel = 0;
    std::uint8_t type = 0;
    std::uint8_t spec = 0;
    Performance performance;

    friend bool operator==(const PerformanceDump&, const PerformanceDump&) = default;
};

// Empty unless `message` is a complete Performance dump (715 bytes) whose data bytes are all nibbles.
std::optional<PerformanceDump> parsePerformanceDump(std::span<const std::uint8_t> message);

std::vector<std::uint8_t> toSysEx(const PerformanceDump& dump);

} // namespace nl2x
