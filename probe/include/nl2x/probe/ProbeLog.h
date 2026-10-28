#pragma once

// The probe's log: every MIDI message in and out, and the scripts' notes, one line each, in a
// format meant to be parsed later:
//
//   +12345.6 ms  OUT   F0 33 0F 04 0E 00 F7
//   +12391.0 ms  IN    F0 33 0F 04 00 00 … F7
//   +12391.0 ms  NOTE  SysEx in: 139 bytes, Program dump, dev 15, type 0, spec 0
//
// Fields are separated by whitespace: time since the log was started or cleared (ms, one decimal),
// the word "ms", the direction (IN, OUT or NOTE), then the bytes in hex or the note text.

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nl2x::probe {

enum class Direction : std::uint8_t { In, Out, Note };

// "F0 33 0F 04 0E 00 F7".
std::string toHex(std::span<const std::uint8_t> bytes);

struct RawMessage {
    std::vector<std::uint8_t> bytes;
    // Empty when `bytes` is a complete, valid MIDI message.
    std::string error;
};

// Parses hex typed by the user ("F0 33 0F 04 0E 00 F7", "b0 4a 40", "F0330F040E00F7"). Accepts one
// complete channel message or one SysEx message, nothing else: a malformed message must never
// reach the synth.
RawMessage parseRawMessage(std::string_view text);

class ProbeLog {
public:
    // `nowMs` is on the caller's clock; the log shows times relative to the last clear().
    explicit ProbeLog(double nowMs) : originMs(nowMs) {}

    void clear(double nowMs);
    // Each returns the line it added.
    const std::string& addMidi(double nowMs, Direction direction, std::span<const std::uint8_t> bytes);
    const std::string& addNote(double nowMs, std::string_view text);

    const std::vector<std::string>& lines() const { return entries; }
    // All lines, each ending with '\n': the content of a saved log file.
    std::string text() const;

private:
    const std::string& add(double nowMs, Direction direction, std::string_view payload);

    double originMs;
    std::vector<std::string> entries;
};

} // namespace nl2x::probe
