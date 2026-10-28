// SysEx framing, nibble coding, request builders and the classifier (research §3).

#include "nl2x/HardwareAssumptions.h"
#include "nl2x/SysEx.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cstdint>
#include <vector>

using namespace nl2x;

namespace {

using Bytes = std::vector<std::uint8_t>;

// Every signed byte value, repeated to fill `count` parameters: covers values outside every
// documented range, which must survive unchanged.
std::vector<std::int8_t> everyValue(std::size_t count)
{
    std::vector<std::int8_t> values(count);
    for (std::size_t i = 0; i < count; ++i)
        values[i] = static_cast<std::int8_t>(static_cast<std::uint8_t>(i * 37 + 11));
    return values;
}

} // namespace

TEST_CASE("Nibbles: low nibble first, signed values in two's complement")
{
    const std::vector<std::int8_t> values { 0, 1, 15, 16, 127, -1, -128, 0x5A };
    const Bytes expected { 0x00, 0x00, 0x01, 0x00, 0x0F, 0x00, 0x00, 0x01, 0x0F, 0x07,
                           0x0F, 0x0F, 0x00, 0x08, 0x0A, 0x05 };

    CHECK(encodeNibbles(values) == expected);
    CHECK(decodeNibbles(expected) == values);
}

TEST_CASE("Nibbles: every one of the 256 byte values round-trips")
{
    std::vector<std::int8_t> values;
    for (int byte = 0; byte < 256; ++byte)
        values.push_back(static_cast<std::int8_t>(static_cast<std::uint8_t>(byte)));

    CHECK(decodeNibbles(encodeNibbles(values)) == values);
}

TEST_CASE("Nibbles: data that cannot round-trip is refused, not masked")
{
    CHECK_FALSE(decodeNibbles(Bytes { 0x10, 0x00 }).has_value());
    CHECK_FALSE(decodeNibbles(Bytes { 0x00, 0x7F }).has_value());
    CHECK_FALSE(decodeNibbles(Bytes { 0x01 }).has_value());
    CHECK(decodeNibbles(Bytes {}) == std::vector<std::int8_t> {});
}

TEST_CASE("Requests are framed F0 33 <dev> 04 <type> <spec> F7")
{
    CHECK(buildRequest(0x0F, 0x0E, 0x02) == Bytes { 0xF0, 0x33, 0x0F, 0x04, 0x0E, 0x02, 0xF7 });
    CHECK(slotEditBufferRequest(3, Slot::C) == Bytes { 0xF0, 0x33, 0x03, 0x04, hardware::kSlotEditBufferRequestType, 2, 0xF7 });
    CHECK(allControllersRequest(0, Slot::A) == Bytes { 0xF0, 0x33, 0x00, 0x04, 0x14, 0x00, 0xF7 });
    CHECK(performanceEditBufferRequest(15) == Bytes { 0xF0, 0x33, 0x0F, 0x04, 40, 0, 0xF7 });
    CHECK(performanceBankRequest(15, 1, 7) == Bytes { 0xF0, 0x33, 0x0F, 0x04, 41, 7, 0xF7 });
    CHECK(performanceBankRequest(15, 4, 99) == Bytes { 0xF0, 0x33, 0x0F, 0x04, 44, 99, 0xF7 });
    CHECK(bankLocationRequest(15, 17, 98) == Bytes { 0xF0, 0x33, 0x0F, 0x04, 17, 98, 0xF7 });
}

TEST_CASE("Dumps have the documented sizes: Program 139, Percussion Kit 1063, Performance 715")
{
    CHECK(programEditBufferDump(15, Slot::A, everyValue(kProgramParameterCount)).size() == 139);
    CHECK(buildSysEx(15, 0, 16, everyValue(kPercussionKitParameterCount)).size() == 1063);
    CHECK(performanceEditBufferDump(15, everyValue(kPerformanceParameterCount)).size() == 715);
}

TEST_CASE("Dump headers carry the Edit Buffer and Bank types")
{
    const auto program = everyValue(kProgramParameterCount);

    const auto toSlotD = programEditBufferDump(9, Slot::D, program);
    CHECK(Bytes(toSlotD.begin(), toSlotD.begin() + 6) == Bytes { 0xF0, 0x33, 9, 0x04, 0, 3 });

    const auto toBank = bankProgramDump(9, 4, 98, program);
    CHECK(Bytes(toBank.begin(), toBank.begin() + 6) == Bytes { 0xF0, 0x33, 9, 0x04, 4, 98 });

    const auto performance = performanceEditBufferDump(9, everyValue(kPerformanceParameterCount));
    CHECK(Bytes(performance.begin(), performance.begin() + 6) == Bytes { 0xF0, 0x33, 9, 0x04, 30, 0 });
}

TEST_CASE("The classifier recognises dumps by size and decodes their values")
{
    const auto [count, kind] = GENERATE(table<std::size_t, SysExKind>({
        { kProgramParameterCount, SysExKind::ProgramDump },
        { kPercussionKitParameterCount, SysExKind::PercussionKitDump },
        { kPerformanceParameterCount, SysExKind::PerformanceDump },
        { 0, SysExKind::Other },
        { 5, SysExKind::Other },
    }));
    const auto values = everyValue(count);
    const auto message = buildSysEx(12, 7, 99, values);

    const auto parsed = parseSysEx(message);

    CHECK(parsed.kind == kind);
    CHECK(parsed.size == message.size());
    CHECK(parsed.globalChannel == 12);
    CHECK(parsed.type == 7);
    CHECK(parsed.spec == 99);
    CHECK(parsed.values == values);
}

TEST_CASE("A received dump rebuilds byte for byte, whatever its values")
{
    const auto count = GENERATE(kProgramParameterCount, kPercussionKitParameterCount, kPerformanceParameterCount);
    const auto original = buildSysEx(15, 3, 42, everyValue(count));

    const auto parsed = parseSysEx(original);
    REQUIRE(parsed.values);

    CHECK(buildSysEx(parsed.globalChannel, parsed.type, parsed.spec, *parsed.values) == original);
}

TEST_CASE("Messages that are not Nord Lead SysEx are classified as such")
{
    CHECK(parseSysEx(Bytes { 0xB0, 74, 10 }).kind == SysExKind::NotNordLead);
    CHECK(parseSysEx(Bytes { 0xF0, 0x41, 0x10, 0x04, 0, 0, 0xF7 }).kind == SysExKind::NotNordLead);
    CHECK(parseSysEx(Bytes { 0xF0, 0x33, 0x0F, 0x05, 0, 0, 0xF7 }).kind == SysExKind::NotNordLead);
    CHECK(parseSysEx(Bytes { 0xF0, 0x33, 0x0F, 0x04, 0, 0 }).kind == SysExKind::NotNordLead);
    CHECK(parseSysEx(Bytes { 0xF0, 0xF7 }).kind == SysExKind::NotNordLead);
    CHECK(parseSysEx(Bytes {}).kind == SysExKind::NotNordLead);
}

TEST_CASE("A dump whose data bytes are not nibbles keeps its kind but has no values")
{
    auto message = programEditBufferDump(15, Slot::A, everyValue(kProgramParameterCount));
    message[10] = 0x40;

    const auto parsed = parseSysEx(message);

    CHECK(parsed.kind == SysExKind::ProgramDump);
    CHECK_FALSE(parsed.values.has_value());
}
