// Performances to and from SysEx dumps, and the Performance settings table: byte-for-byte
// round-trips including the four embedded Programs, values outside the documented ranges kept as
// they are (research §3). Fixtures are generated from values we chose.

#include "nl2x/Performance.h"
#include "nl2x/PerformanceSettings.h"
#include "nl2x/SysEx.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <set>
#include <string>
#include <vector>

using namespace nl2x;

namespace {

using Bytes = std::vector<std::uint8_t>;

Performance::Values fixtureValues(int seed)
{
    Performance::Values values;
    for (std::size_t i = 0; i < values.size(); ++i)
        values[i] = static_cast<std::int8_t>(static_cast<std::uint8_t>(i * 37 + static_cast<std::size_t>(seed) * 13 + 7));
    return values;
}

} // namespace

TEST_CASE("Performance dump: reading and writing back gives the same bytes")
{
    const auto bytes = buildSysEx(0x0F, 31, 42, fixtureValues(1));
    REQUIRE(bytes.size() == kPerformanceDumpSize);

    const auto dump = parsePerformanceDump(bytes);
    REQUIRE(dump);
    CHECK(dump->globalChannel == 0x0F);
    CHECK(dump->type == 31);
    CHECK(dump->spec == 42);
    CHECK(dump->performance.bytes() == fixtureValues(1));
    CHECK(toSysEx(*dump) == bytes);
}

TEST_CASE("Performance dump: every one of the 256 byte values survives in every position")
{
    for (int seed = 0; seed < 256; ++seed) {
        const PerformanceDump dump { 3, 30, 0, Performance(fixtureValues(seed)) };
        const auto parsed = parsePerformanceDump(toSysEx(dump));
        REQUIRE(parsed);
        CHECK(*parsed == dump);
    }
}

TEST_CASE("Performance dump: the four Programs are Slots A–D, 66 bytes each, in order")
{
    const Performance performance(fixtureValues(2));
    for (auto slot : kAllSlots) {
        const auto program = performance.program(slot);
        for (std::size_t i = 0; i < kProgramParameterCount; ++i)
            CHECK(program.bytes()[i] == performance.bytes()[slotIndex(slot) * kProgramParameterCount + i]);
    }

    auto changed = performance;
    Program program;
    program.setValue(ProgramParameter::FilterCutoff, -7);
    changed.setProgram(Slot::C, program);
    CHECK(changed.program(Slot::C) == program);
    CHECK(changed.program(Slot::B) == performance.program(Slot::B));
    CHECK(changed.setting(PerformanceSetting::SplitPoint) == performance.setting(PerformanceSetting::SplitPoint));
}

TEST_CASE("Performance dump: values outside the documented ranges are kept, never clamped")
{
    Performance::Values values {};
    values[264] = -1;  // Slot A's channel: off [OFF-FB]
    values[312] = 14;  // Pitch Bend Range, documented 0–8, found in [OFF-FB]
    values[321] = -94; // Keyboard Octave Shift, constant in [OFF-FB]
    values[353] = -128;
    const PerformanceDump dump { 0, 30, 0, Performance(values) };

    const auto parsed = parsePerformanceDump(toSysEx(dump));
    REQUIRE(parsed);
    CHECK(parsed->performance.setting(PerformanceSetting::SlotChannel, Slot::A) == -1);
    CHECK(parsed->performance.setting(PerformanceSetting::PitchBendRange) == 14);
    CHECK(parsed->performance.setting(PerformanceSetting::KeyboardOctaveShift) == -94);
    CHECK(parsed->performance.setting(PerformanceSetting::SplitPoint) == -128);
}

TEST_CASE("Performance dump: anything but a 715-byte dump of nibbles is not a Performance")
{
    CHECK_FALSE(parsePerformanceDump(buildSysEx(0, 0, 0, Program().bytes())));
    auto bytes = buildSysEx(0, 30, 0, fixtureValues(3));
    bytes[100] = 0x10;
    CHECK_FALSE(parsePerformanceDump(bytes));
}

TEST_CASE("Performance settings: per-Slot settings address one byte per Slot")
{
    Performance performance;
    performance.setSetting(PerformanceSetting::Layer, Slot::A, 1);
    performance.setSetting(PerformanceSetting::Layer, Slot::D, 1);
    performance.setSetting(PerformanceSetting::SlotChannel, Slot::C, 9);

    CHECK(performance.bytes()[324] == 1);
    CHECK(performance.bytes()[325] == 0);
    CHECK(performance.bytes()[327] == 1);
    CHECK(performance.bytes()[266] == 9);
    CHECK(performance.setting(PerformanceSetting::Layer, Slot::D) == 1);
}

TEST_CASE("Performance settings: Out Mode A/B and C/D share one byte, low and high nibble")
{
    Performance::Values values {};
    values[314] = static_cast<std::int8_t>(0xA2);
    Performance performance(values);

    CHECK(performance.setting(PerformanceSetting::OutModeAB) == 2);
    CHECK(performance.setting(PerformanceSetting::OutModeCD) == 10);

    performance.setSetting(PerformanceSetting::OutModeAB, Slot::A, 3);
    CHECK(static_cast<std::uint8_t>(performance.bytes()[314]) == 0xA3);
    performance.setSetting(PerformanceSetting::OutModeCD, Slot::A, 1);
    CHECK(static_cast<std::uint8_t>(performance.bytes()[314]) == 0x13);
}

TEST_CASE("Performance settings table: every byte 264–353 belongs to exactly one setting")
{
    std::multiset<std::size_t> covered;
    for (const auto& info : performanceSettings())
        for (auto slot : kAllSlots) {
            if (! info.perSlot && slot != Slot::A)
                continue;
            // The two Out Mode nibbles share their byte.
            if (info.id == PerformanceSetting::OutModeCD)
                continue;
            covered.insert(performanceSettingOffset(info.id, slot));
        }

    CHECK(covered.size() == kPerformanceSettingsCount);
    for (std::size_t offset = kPerformanceSlotChannelOffset; offset < kPerformanceParameterCount; ++offset)
        CHECK(covered.count(offset) == 1);
}

TEST_CASE("Performance settings table: the settings the synth does not receive are flagged")
{
    std::set<std::size_t> notReceived;
    for (const auto& info : performanceSettings())
        if (info.access == PerformanceSettingAccess::NotReceived)
            notReceived.insert(info.offset);

    // "not received!!!" [OFF-2X p.112].
    const std::set<std::size_t> expected { 315, 316, 317, 318, 319, 320, 321, 323 };
    CHECK(notReceived == expected);
}

TEST_CASE("Performance settings: DAW parameter IDs and names")
{
    CHECK(performanceDawParameterId(PerformanceSetting::KeyboardSplit, Slot::A) == "perf_split");
    CHECK(performanceDawParameterId(PerformanceSetting::SplitPoint, Slot::C) == "perf_splitPoint");
    CHECK(performanceDawParameterId(PerformanceSetting::Layer, Slot::B) == "perf_layerB");
    CHECK(performanceDawParameterName(PerformanceSetting::SplitPoint, Slot::A) == "Performance: Split Point");
    CHECK(performanceDawParameterName(PerformanceSetting::Layer, Slot::D) == "Performance: Slot D Layer");

    std::set<std::string> keys;
    for (const auto& info : performanceSettings())
        CHECK(keys.insert(std::string(info.key)).second);
}
