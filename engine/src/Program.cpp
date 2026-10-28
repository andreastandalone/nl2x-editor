#include "nl2x/Program.h"

#include <algorithm>
#include <cassert>

namespace nl2x {

std::optional<Program> Program::fromValues(std::span<const std::int8_t> programValues)
{
    if (programValues.size() != kProgramParameterCount)
        return std::nullopt;
    Values values;
    std::copy(programValues.begin(), programValues.end(), values.begin());
    return Program(values);
}

void Program::setValue(ProgramParameter parameter, int newValue)
{
    assert(newValue >= -128 && newValue <= 127);
    values[static_cast<std::size_t>(parameter)] = static_cast<std::int8_t>(newValue);
}

std::optional<ProgramDump> parseProgramDump(std::span<const std::uint8_t> message)
{
    const auto parsed = parseSysEx(message);
    if (parsed.kind != SysExKind::ProgramDump || ! parsed.values)
        return std::nullopt;
    return ProgramDump { parsed.globalChannel, parsed.type, parsed.spec, *Program::fromValues(*parsed.values) };
}

std::optional<PercussionKitDump> parsePercussionKitDump(std::span<const std::uint8_t> message)
{
    const auto parsed = parseSysEx(message);
    if (parsed.kind != SysExKind::PercussionKitDump || ! parsed.values)
        return std::nullopt;

    PercussionKitDump dump { parsed.globalChannel, parsed.type, parsed.spec, {} };
    const std::span<const std::int8_t> values = *parsed.values;
    for (std::size_t i = 0; i < kPercussionKitProgramCount; ++i)
        dump.kit[i] = *Program::fromValues(values.subspan(i * kProgramParameterCount, kProgramParameterCount));
    return dump;
}

std::vector<std::uint8_t> toSysEx(const ProgramDump& dump)
{
    return buildSysEx(dump.globalChannel, dump.type, dump.spec, dump.program.bytes());
}

std::vector<std::uint8_t> toSysEx(const PercussionKitDump& dump)
{
    std::vector<std::int8_t> values;
    values.reserve(kPercussionKitParameterCount);
    for (const auto& program : dump.kit)
        values.insert(values.end(), program.bytes().begin(), program.bytes().end());
    return buildSysEx(dump.globalChannel, dump.type, dump.spec, values);
}

std::vector<std::vector<std::uint8_t>> splitSysExMessages(std::span<const std::uint8_t> bytes)
{
    std::vector<std::vector<std::uint8_t>> messages;
    auto start = std::find(bytes.begin(), bytes.end(), kSysExStart);
    while (start != bytes.end()) {
        const auto end = std::find(start + 1, bytes.end(), kSysExEnd);
        if (end == bytes.end())
            break;
        // A new F0 before the F7 means the earlier message was cut off: drop it.
        const auto restart = std::find(start + 1, end, kSysExStart);
        if (restart != end) {
            start = restart;
            continue;
        }
        messages.emplace_back(start, end + 1);
        start = std::find(end + 1, bytes.end(), kSysExStart);
    }
    return messages;
}

} // namespace nl2x
