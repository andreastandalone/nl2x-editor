#pragma once

// Programs and Percussion Kits as values, and their conversion to and from SysEx dumps
// (research §3). Every byte is kept exactly as received: values outside the documented ranges
// are never clamped, so reading a dump and writing it back gives the same bytes.

#include "nl2x/ProgramParameters.h"
#include "nl2x/SysEx.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace nl2x {

// The 66 signed parameter bytes of one Program, by decoded offset [OFF-2X p.111–112].
class Program {
public:
    using Values = std::array<std::int8_t, kProgramParameterCount>;

    // All zero: not a meaningful sound, only a starting point.
    Program() = default;
    explicit Program(const Values& programValues) : values(programValues) {}
    // Empty unless `programValues` holds exactly kProgramParameterCount values.
    static std::optional<Program> fromValues(std::span<const std::int8_t> programValues);

    int value(ProgramParameter parameter) const { return values[static_cast<std::size_t>(parameter)]; }
    // `newValue` is stored as a signed byte; the caller keeps it within −128…127.
    void setValue(ProgramParameter parameter, int newValue);

    const Values& bytes() const { return values; }

    friend bool operator==(const Program&, const Program&) = default;

private:
    Values values {};
};

// Eight Programs played as one drum sound [OFF-2X p.28, 110].
using PercussionKit = std::array<Program, kPercussionKitProgramCount>;

// A Program dump as it travels: the header bytes are kept so that a dump read from a file or the
// synth is written back unchanged, even where the type byte is unreliable (research §3).
struct ProgramDump {
    GlobalChannel globalChannel = 0;
    std::uint8_t type = 0;
    std::uint8_t spec = 0;
    Program program;

    friend bool operator==(const ProgramDump&, const ProgramDump&) = default;
};

struct PercussionKitDump {
    GlobalChannel globalChannel = 0;
    std::uint8_t type = 0;
    std::uint8_t spec = 0;
    PercussionKit kit;

    friend bool operator==(const PercussionKitDump&, const PercussionKitDump&) = default;
};

// Empty unless `message` is a complete Program (139 bytes) or Percussion Kit (1063 bytes) dump
// whose data bytes are all nibbles.
std::optional<ProgramDump> parseProgramDump(std::span<const std::uint8_t> message);
std::optional<PercussionKitDump> parsePercussionKitDump(std::span<const std::uint8_t> message);

std::vector<std::uint8_t> toSysEx(const ProgramDump& dump);
std::vector<std::uint8_t> toSysEx(const PercussionKitDump& dump);

// The complete SysEx messages in a .syx file's bytes (F0 … F7), in order. Bytes outside a message
// are skipped; an unterminated message at the end is dropped.
std::vector<std::vector<std::uint8_t>> splitSysExMessages(std::span<const std::uint8_t> bytes);

} // namespace nl2x
