#include "nl2x/Performance.h"

#include <algorithm>
#include <cassert>

namespace nl2x {

namespace {

std::size_t programOffset(Slot slot)
{
    return slotIndex(slot) * kProgramParameterCount;
}

} // namespace

std::optional<Performance> Performance::fromValues(std::span<const std::int8_t> performanceValues)
{
    if (performanceValues.size() != kPerformanceParameterCount)
        return std::nullopt;
    Values values;
    std::copy(performanceValues.begin(), performanceValues.end(), values.begin());
    return Performance(values);
}

Program Performance::program(Slot slot) const
{
    return *Program::fromValues(std::span<const std::int8_t>(values).subspan(programOffset(slot), kProgramParameterCount));
}

void Performance::setProgram(Slot slot, const Program& program)
{
    std::copy(program.bytes().begin(), program.bytes().end(), values.begin() + static_cast<std::ptrdiff_t>(programOffset(slot)));
}

int Performance::setting(PerformanceSetting setting, Slot slot) const
{
    const auto byte = values[performanceSettingOffset(setting, slot)];
    const auto bits = static_cast<std::uint8_t>(byte);
    switch (performanceSettingInfo(setting).bits) {
    case PerformanceSettingBits::Byte: return byte;
    case PerformanceSettingBits::LowNibble: return bits & 0x0F;
    case PerformanceSettingBits::HighNibble: return bits >> 4;
    }
    return byte;
}

void Performance::setSetting(PerformanceSetting setting, Slot slot, int newValue)
{
    auto& byte = values[performanceSettingOffset(setting, slot)];
    const auto bits = static_cast<std::uint8_t>(byte);
    switch (performanceSettingInfo(setting).bits) {
    case PerformanceSettingBits::Byte:
        assert(newValue >= -128 && newValue <= 127);
        byte = static_cast<std::int8_t>(newValue);
        break;
    case PerformanceSettingBits::LowNibble:
        assert(newValue >= 0 && newValue <= 15);
        byte = static_cast<std::int8_t>((bits & 0xF0) | (newValue & 0x0F));
        break;
    case PerformanceSettingBits::HighNibble:
        assert(newValue >= 0 && newValue <= 15);
        byte = static_cast<std::int8_t>((bits & 0x0F) | ((newValue & 0x0F) << 4));
        break;
    }
}

std::optional<PerformanceDump> parsePerformanceDump(std::span<const std::uint8_t> message)
{
    const auto parsed = parseSysEx(message);
    if (parsed.kind != SysExKind::PerformanceDump || ! parsed.values)
        return std::nullopt;
    return PerformanceDump { parsed.globalChannel, parsed.type, parsed.spec, *Performance::fromValues(*parsed.values) };
}

std::vector<std::uint8_t> toSysEx(const PerformanceDump& dump)
{
    return buildSysEx(dump.globalChannel, dump.type, dump.spec, dump.performance.bytes());
}

} // namespace nl2x
