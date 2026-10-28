// The probe's log lines and the raw hex entry.

#include "nl2x/probe/ProbeLog.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

using namespace nl2x::probe;

using Bytes = std::vector<std::uint8_t>;

TEST_CASE("Log lines: time since the start, direction, then the bytes or the note")
{
    ProbeLog log(1000.0);

    CHECK(log.addMidi(1012.34, Direction::Out, Bytes { 0xF0, 0x33, 0x0F, 0x04, 0x0E, 0x00, 0xF7 })
          == "+12.3 ms  OUT   F0 33 0F 04 0E 00 F7");
    CHECK(log.addMidi(13345.6, Direction::In, Bytes { 0xB0, 0x4A, 0x7F }) == "+12345.6 ms  IN    B0 4A 7F");
    CHECK(log.addNote(13346.0, "SUMMARY x: y") == "+12346.0 ms  NOTE  SUMMARY x: y");
    CHECK(log.text()
          == "+12.3 ms  OUT   F0 33 0F 04 0E 00 F7\n+12345.6 ms  IN    B0 4A 7F\n+12346.0 ms  NOTE  SUMMARY x: y\n");
}

TEST_CASE("Clearing the log restarts its clock")
{
    ProbeLog log(0.0);
    log.addNote(5.0, "a");

    log.clear(500.0);
    log.addNote(501.0, "b");

    CHECK(log.lines() == std::vector<std::string> { "+1.0 ms  NOTE  b" });
}

TEST_CASE("Raw entry accepts SysEx and channel messages in any common hex spelling")
{
    CHECK(parseRawMessage("F0 33 0F 04 0E 00 F7").bytes == Bytes { 0xF0, 0x33, 0x0F, 0x04, 0x0E, 0x00, 0xF7 });
    CHECK(parseRawMessage("f0330f040e00f7").bytes == Bytes { 0xF0, 0x33, 0x0F, 0x04, 0x0E, 0x00, 0xF7 });
    CHECK(parseRawMessage("0xB0, 0x4A, 0x40").bytes == Bytes { 0xB0, 0x4A, 0x40 });
    CHECK(parseRawMessage(" c0 05 ").bytes == Bytes { 0xC0, 0x05 });
    CHECK(parseRawMessage("b0 4a 40").error.empty());
}

TEST_CASE("Raw entry refuses anything that is not one complete MIDI message")
{
    for (const auto* text : { "", "F0 33 0F", "F0 33 8F F7", "B0 4A", "B0 4A 40 00", "4A 40", "F8", "B0 4A 80",
                              "F0 3", "F0 zz F7" }) {
        INFO(text);
        const auto parsed = parseRawMessage(text);
        CHECK_FALSE(parsed.error.empty());
        CHECK(parsed.bytes.empty());
    }
}
