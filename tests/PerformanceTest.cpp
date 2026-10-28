// The Performance (#5): reading the Performance Edit Buffer, Slot Channels taken from it, manual
// overrides, the shared-channel warning and editing Performance settings. The engine is driven as
// the shell drives it, wired to the simulated synth; tests look at the MIDI it sends and its
// public state.

#include "EditorAndSynth.h"

#include "nl2x/HardwareAssumptions.h"
#include "nl2x/Performance.h"
#include "nl2x/SysEx.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstdint>
#include <vector>

using namespace nl2x;
using nl2x::test::EditorAndSynth;
using nl2x::test::SimulatedNordLead2X;
using Catch::Matchers::ContainsSubstring;

namespace {

using Bytes = std::vector<std::uint8_t>;

constexpr std::uint8_t kFilterCutoffCc = 74;
constexpr std::size_t kFilterCutoffOffset = 3;

Bytes cutoffCc(MidiChannel channel, std::uint8_t value)
{
    return { static_cast<std::uint8_t>(0xB0 | channel), kFilterCutoffCc, value };
}

std::size_t settingIndex(std::size_t offset)
{
    return offset - kPerformanceSlotChannelOffset;
}

// Synth settings with the global, not-received bytes set to values we can recognise.
SimulatedNordLead2X::PerformanceSettings recognisableSettings()
{
    SimulatedNordLead2X::PerformanceSettings settings {};
    settings[settingIndex(315)] = 15;  // Global MIDI Channel
    settings[settingIndex(316)] = 1;   // Prog. Change On/Off
    settings[settingIndex(317)] = 1;   // Controllers On/Off
    settings[settingIndex(318)] = -12; // Master Tune
    settings[settingIndex(321)] = -94; // Keyboard Octave Shift, as in [OFF-FB]
    settings[settingIndex(323)] = 17;  // Arp. MIDI Out, as in [OFF-FB]
    settings[settingIndex(324)] = 1;   // Slot A in the Layer
    settings[settingIndex(353)] = 60;  // Split Point
    return settings;
}

} // namespace

TEST_CASE("Connecting reads the Performance Edit Buffer once the Global Channel is known")
{
    EditorAndSynth rig;
    rig.synth.setGlobalChannel(4);
    rig.synth.setPerformanceSettings(recognisableSettings());

    rig.connect();

    CHECK(rig.toSynth.messages().back() == performanceEditBufferRequest(4));
    CHECK(rig.engine.performanceStatus() == PerformanceStatus::Read);
    REQUIRE(rig.engine.performance());
    CHECK(rig.engine.performance()->setting(PerformanceSetting::SplitPoint) == 60);
    CHECK(rig.engine.performance()->setting(PerformanceSetting::Layer, Slot::A) == 1);
}

TEST_CASE("After connecting, a Slot's CCs go out on the Slot Channel read from the Performance")
{
    EditorAndSynth rig;
    rig.synth.setSlotChannel(Slot::A, 5);
    rig.synth.setSlotChannel(Slot::B, 12);

    rig.connect();
    rig.toSynth.clear();
    rig.engine.setParameter(Slot::A, CcParameter::FilterCutoff, 77);
    rig.engine.setParameter(Slot::B, CcParameter::FilterCutoff, 33);

    const std::vector<Bytes> expected { cutoffCc(5, 77), cutoffCc(12, 33) };
    CHECK(rig.toSynth.messages() == expected);
    CHECK(rig.engine.slotChannel(Slot::A) == MidiChannel { 5 });
    CHECK(rig.synth.editBuffer(Slot::A)[kFilterCutoffOffset] == 77);
    CHECK(rig.synth.editBuffer(Slot::B)[kFilterCutoffOffset] == 33);
}

TEST_CASE("Before the Performance is read, CCs go out on the default Slot Channels")
{
    EditorAndSynth rig;

    rig.engine.setParameter(Slot::C, CcParameter::FilterCutoff, 10);

    const std::vector<Bytes> expected { cutoffCc(hardware::kDefaultSlotChannels[2], 10) };
    CHECK(rig.toSynth.messages() == expected);
}

TEST_CASE("A Slot whose channel is off (−1) in the Performance sends nothing")
{
    EditorAndSynth rig;
    rig.synth.setSlotChannel(Slot::B, std::nullopt);

    rig.connect();
    rig.toSynth.clear();
    rig.engine.setParameter(Slot::B, CcParameter::FilterCutoff, 50);

    CHECK(rig.toSynth.messages().empty());
    CHECK_FALSE(rig.engine.slotChannel(Slot::B));
}

TEST_CASE("A manual Slot Channel override wins over the Performance, and can be released")
{
    EditorAndSynth rig;
    rig.synth.setSlotChannel(Slot::A, 0);
    rig.synth.setSlotChannel(Slot::D, std::nullopt);
    rig.connect();

    rig.engine.setSlotChannelOverride(Slot::A, 9);
    rig.engine.setSlotChannelOverride(Slot::D, 2);
    rig.toSynth.clear();
    rig.engine.setParameter(Slot::A, CcParameter::FilterCutoff, 1);
    rig.engine.setParameter(Slot::D, CcParameter::FilterCutoff, 2);

    std::vector<Bytes> expected { cutoffCc(9, 1), cutoffCc(2, 2) };
    CHECK(rig.toSynth.messages() == expected);

    SECTION("a later Performance read does not change an override")
    {
        rig.synth.setSlotChannel(Slot::A, 7);
        rig.engine.readPerformance(rig.now);
        rig.runUntilSettled();
        CHECK(rig.engine.slotChannel(Slot::A) == MidiChannel { 9 });
    }

    SECTION("released, the Performance's channel applies again")
    {
        rig.engine.setSlotChannelOverride(Slot::A, std::nullopt);
        rig.toSynth.clear();
        rig.engine.setParameter(Slot::A, CcParameter::FilterCutoff, 3);
        expected = { cutoffCc(0, 3) };
        CHECK(rig.toSynth.messages() == expected);
    }
}

TEST_CASE("Read from synth for the Performance picks up Slot Channels changed on the synth")
{
    EditorAndSynth rig;
    rig.connect();
    const auto channel = *rig.engine.globalChannel();
    rig.synth.setSlotChannel(Slot::C, 14);
    rig.toSynth.clear();

    rig.engine.readPerformance(rig.now);
    rig.runUntilSettled();

    const std::vector<Bytes> expected { performanceEditBufferRequest(channel) };
    CHECK(rig.toSynth.messages() == expected);
    CHECK(rig.engine.slotChannel(Slot::C) == MidiChannel { 14 });
}

TEST_CASE("Shared-channel warning names the Slots on the same channel")
{
    EditorAndSynth rig;
    rig.synth.setSlotChannel(Slot::A, 0);
    rig.synth.setSlotChannel(Slot::B, 0);
    rig.synth.setSlotChannel(Slot::C, 2);
    rig.synth.setSlotChannel(Slot::D, std::nullopt);
    rig.connect();

    auto warnings = rig.engine.slotChannelWarnings();
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].kind == SlotChannelWarning::Kind::SharedChannel);
    CHECK(warnings[0].channel == 0);
    CHECK(warnings[0].slots == std::vector<Slot> { Slot::A, Slot::B });
    CHECK(slotChannelWarningText(warnings[0])
          == "Slots A and B share MIDI channel 1: a knob change affects both.");

    SECTION("including Slots brought together by a manual override")
    {
        rig.engine.setSlotChannelOverride(Slot::D, 0);
        warnings = rig.engine.slotChannelWarnings();
        REQUIRE(warnings.size() == 1);
        CHECK(warnings[0].slots == std::vector<Slot> { Slot::A, Slot::B, Slot::D });
        CHECK_THAT(slotChannelWarningText(warnings[0]), ContainsSubstring("Slots A, B and D share MIDI channel 1"));
    }

    SECTION("and none once an override separates them")
    {
        rig.engine.setSlotChannelOverride(Slot::B, 5);
        CHECK(rig.engine.slotChannelWarnings().empty());
    }
}

TEST_CASE("Layer warning: layered Slots also receive on the leftmost active Slot's channel")
{
    EditorAndSynth rig;
    for (auto slot : kAllSlots)
        rig.synth.setSlotChannel(slot, static_cast<MidiChannel>(slotIndex(slot)));
    rig.synth.setLayered(Slot::A, true);
    rig.synth.setLayered(Slot::B, true);
    rig.synth.setLayered(Slot::D, true);
    rig.connect();

    const auto warnings = rig.engine.slotChannelWarnings();
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].kind == SlotChannelWarning::Kind::Layer);
    CHECK(warnings[0].channel == 0);
    CHECK(warnings[0].slots == std::vector<Slot> { Slot::A, Slot::B, Slot::D });
    CHECK_THAT(slotChannelWarningText(warnings[0]), ContainsSubstring("Slots B and D are layered with Slot A"));

    // The warning is right: a knob change on Slot A reaches B and D on the synth.
    rig.engine.setParameter(Slot::A, CcParameter::FilterCutoff, 101);
    CHECK(rig.synth.editBuffer(Slot::B)[kFilterCutoffOffset] == 101);
    CHECK(rig.synth.editBuffer(Slot::D)[kFilterCutoffOffset] == 101);
    CHECK(rig.synth.editBuffer(Slot::C)[kFilterCutoffOffset] != 101);
}

TEST_CASE("Editing the Split Point sends exactly one coalesced Performance dump")
{
    EditorAndSynth rig;
    rig.synth.setPerformanceSettings(recognisableSettings());
    rig.connect();
    const auto channel = *rig.engine.globalChannel();
    const auto read = *rig.engine.performance();
    rig.toSynth.clear();

    // A slider dragged within one timer tick.
    for (int value : { 61, 64, 67, 72 })
        CHECK(rig.engine.setPerformanceSetting(PerformanceSetting::SplitPoint, Slot::A, value)
              == PerformanceEditResult::Accepted);
    rig.runFor(1000);

    REQUIRE(rig.toSynth.messages().size() == 1);
    const auto dump = parsePerformanceDump(rig.toSynth.messages().front());
    REQUIRE(dump);
    CHECK(dump->globalChannel == channel);
    CHECK(dump->type == hardware::kPerformanceEditBufferDumpType);
    CHECK(dump->spec == 0);
    CHECK(dump->performance.setting(PerformanceSetting::SplitPoint) == 72);
    // Every other byte is as read, the not-received ones included.
    for (std::size_t offset = 0; offset < kPerformanceParameterCount; ++offset)
        if (offset != 353)
            CHECK(dump->performance.bytes()[offset] == read.bytes()[offset]);

    CHECK(rig.synth.performanceSettings()[settingIndex(353)] == 72);
    CHECK(rig.synth.performanceSettings()[settingIndex(318)] == -12);
    CHECK(rig.engine.performance()->setting(PerformanceSetting::SplitPoint) == 72);
}

TEST_CASE("Edits spread over time go out at most once per coalescing interval")
{
    EditorAndSynth rig;
    rig.connect();
    rig.toSynth.clear();
    rig.sendTimes.clear();

    for (int value = 0; value < 40; ++value) {
        rig.engine.setPerformanceSetting(PerformanceSetting::SplitPoint, Slot::A, value);
        rig.tick();
    }
    rig.runFor(1000);

    const auto dumps = rig.performanceDumpsSent();
    REQUIRE(dumps.size() >= 2);
    CHECK(dumps.size() <= 5);
    for (std::size_t i = 1; i < rig.sendTimes.size(); ++i)
        CHECK(rig.sendTimes[i] - rig.sendTimes[i - 1] >= hardware::kPerformanceDumpIntervalMs);
    // The last value always arrives.
    CHECK(parsePerformanceDump(dumps.back())->performance.setting(PerformanceSetting::SplitPoint) == 39);
}

TEST_CASE("Setting a Performance value it already has sends nothing")
{
    EditorAndSynth rig;
    rig.synth.setPerformanceSettings(recognisableSettings());
    rig.connect();
    rig.toSynth.clear();

    rig.engine.setPerformanceSetting(PerformanceSetting::SplitPoint, Slot::A, 60);
    rig.runFor(500);

    CHECK(rig.toSynth.messages().empty());
}

TEST_CASE("A Performance dump carries the Slots' current Programs, and a Kit Slot's bytes as read")
{
    EditorAndSynth rig;
    SimulatedNordLead2X::PercussionKit kit {};
    kit.fill(4);
    rig.synth.playPercussionKit(Slot::C, kit);
    rig.connect();
    const auto read = *rig.engine.performance();

    rig.engine.setParameter(Slot::A, CcParameter::FilterCutoff, 99);
    rig.toSynth.clear();
    rig.engine.setPerformanceSetting(PerformanceSetting::Layer, Slot::B, 1);
    rig.runFor(500);

    const auto dumps = rig.performanceDumpsSent();
    REQUIRE(dumps.size() == 1);
    const auto sent = parsePerformanceDump(dumps.front())->performance;
    CHECK(sent.program(Slot::A).value(ProgramParameter::FilterCutoff) == 99);
    CHECK(sent.program(Slot::B) == *rig.engine.slotState(Slot::B).program);
    CHECK(sent.program(Slot::C) == read.program(Slot::C));
    CHECK(sent.setting(PerformanceSetting::Layer, Slot::B) == 1);
}

TEST_CASE("Performance edits are refused, and nothing is sent, before the Performance is read")
{
    EditorAndSynth rig;

    SECTION("not connected")
    {
        CHECK(rig.engine.setPerformanceSetting(PerformanceSetting::KeyboardSplit, Slot::A, 1)
              == PerformanceEditResult::RefusedNotRead);
        rig.runFor(1000);
        CHECK(rig.toSynth.messages().empty());
    }

    SECTION("the synth answers the Slot requests but not the Performance request")
    {
        rig.synth.setAnswersPerformanceRequests(false);
        rig.connect();
        rig.toSynth.clear();

        CHECK(rig.engine.connectionStatus() == ConnectionStatus::Connected);
        CHECK(rig.engine.performanceStatus() == PerformanceStatus::NoAnswer);
        CHECK(rig.engine.setPerformanceSetting(PerformanceSetting::KeyboardSplit, Slot::A, 1)
              == PerformanceEditResult::RefusedNotRead);
        rig.runFor(1000);
        CHECK(rig.toSynth.messages().empty());
        // The Slot Channels stay the defaults.
        CHECK(rig.engine.slotChannel(Slot::B) == hardware::kDefaultSlotChannels[1]);
    }
}

TEST_CASE("Settings the synth does not receive are never sent as edits")
{
    EditorAndSynth rig;
    rig.connect();
    rig.toSynth.clear();

    CHECK(rig.engine.setPerformanceSetting(PerformanceSetting::MasterTune, Slot::A, 10)
          == PerformanceEditResult::RefusedNotEditable);
    CHECK(rig.engine.setPerformanceSetting(PerformanceSetting::GlobalChannel, Slot::A, 3)
          == PerformanceEditResult::RefusedNotEditable);
    rig.runFor(1000);

    CHECK(rig.toSynth.messages().empty());
}

TEST_CASE("Editing a Slot Channel in the Performance moves that Slot's CCs and the synth's Slot")
{
    EditorAndSynth rig;
    rig.connect();

    rig.engine.setPerformanceSetting(PerformanceSetting::SlotChannel, Slot::B, 8);
    rig.runFor(500);
    rig.toSynth.clear();
    rig.engine.setParameter(Slot::B, CcParameter::FilterCutoff, 66);

    const std::vector<Bytes> expected { cutoffCc(8, 66) };
    CHECK(rig.toSynth.messages() == expected);
    CHECK(rig.synth.slotChannel(Slot::B) == MidiChannel { 8 });
    CHECK(rig.synth.editBuffer(Slot::B)[kFilterCutoffOffset] == 66);
}

TEST_CASE("An edit made while the Performance is being read again survives the reply")
{
    EditorAndSynth rig;
    rig.connect();
    rig.toSynth.clear();

    rig.engine.readPerformance(rig.now);
    rig.engine.setPerformanceSetting(PerformanceSetting::UnisonDetune, Slot::A, 6);
    rig.runUntilSettled();
    rig.runFor(500);

    CHECK(rig.engine.performance()->setting(PerformanceSetting::UnisonDetune) == 6);
    CHECK(rig.synth.performanceSettings()[settingIndex(313)] == 6);
    CHECK(rig.performanceDumpsSent().size() == 1);
}

TEST_CASE("Connecting again forgets the Performance read from the earlier connection")
{
    EditorAndSynth rig;
    rig.connect();
    REQUIRE(rig.engine.performance());
    rig.synth.setAnswersPerformanceRequests(false);

    rig.connect();

    CHECK_FALSE(rig.engine.performance());
    CHECK(rig.engine.setPerformanceSetting(PerformanceSetting::KeyboardSplit, Slot::A, 1)
          == PerformanceEditResult::RefusedNotRead);
}
