// Every CC parameter of the four Slots, both ways (#6). The engine is driven as the shell drives
// it, wired to the simulated synth; tests look at the MIDI crossing the boundary, the synth's
// Edit Buffers and the engine's public Slot state. The expected CC numbers and offsets below are
// written from research §2–3 on their own, so a mistake in the engine's table fails here.

#include "EditorAndSynth.h"

#include "nl2x/CcParameters.h"
#include "nl2x/HardwareAssumptions.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

using namespace nl2x;
using nl2x::test::EditorAndSynth;
using nl2x::test::SimulatedNordLead2X;

namespace {

using Bytes = std::vector<std::uint8_t>;

enum class Kind { Knob, Stepped, Switch };

struct Expected {
    std::string_view key;
    std::uint8_t cc;
    std::size_t offset;
    // Bits of the byte; 0xFF = all.
    std::uint8_t bits;
    Kind kind;
    // Highest documented value (lowest is 0).
    int maxValue;
};

constexpr std::uint8_t kAll = 0xFF;

// Research §2 (CC chart [OFF-2X p.103–104]) with the SysEx offsets of research §3.
const std::vector<Expected> kExpected {
    { "gain", 7, 17, kAll, Kind::Knob, 127 },
    { "oscMix", 8, 2, kAll, Kind::Knob, 127 },
    { "portamentoTime", 5, 16, kAll, Kind::Knob, 127 },
    { "voiceMode", 15, 58, kAll, Kind::Stepped, 2 },
    { "unison", 16, 60, kAll, Kind::Switch, 1 },
    { "octaveShift", 17, 64, kAll, Kind::Stepped, 4 },
    { "modWheelDestination", 18, 59, kAll, Kind::Stepped, 4 },
    { "lfo1Rate", 19, 21, kAll, Kind::Knob, 127 },
    { "lfo1Waveform", 20, 56, kAll, Kind::Stepped, 4 },
    { "lfo1Destination", 21, 57, kAll, Kind::Stepped, 4 },
    { "lfo1Amount", 22, 22, kAll, Kind::Knob, 127 },
    { "lfo2Rate", 23, 23, kAll, Kind::Knob, 127 },
    { "lfo2DestinationArpMode", 24, 65, kAll, Kind::Stepped, 8 },
    { "lfo2Amount", 25, 24, kAll, Kind::Knob, 127 },
    { "modEnvAttack", 26, 18, kAll, Kind::Knob, 127 },
    { "modEnvDecay", 27, 19, kAll, Kind::Knob, 127 },
    { "modEnvDestination", 28, 61, kAll, Kind::Stepped, 3 },
    { "modEnvAmount", 29, 20, kAll, Kind::Knob, 127 },
    { "osc1Waveform", 30, 50, kAll, Kind::Stepped, 3 },
    { "osc2Waveform", 31, 51, kAll, Kind::Stepped, 3 },
    { "osc2FineTune", 33, 1, kAll, Kind::Knob, 127 },
    { "osc2KeyboardTrack", 34, 54, kAll, Kind::Switch, 1 },
    { "ringModSync", 35, 52, 0x03, Kind::Stepped, 3 },
    { "ampEnvDecay", 36, 13, kAll, Kind::Knob, 127 },
    { "ampEnvSustain", 37, 14, kAll, Kind::Knob, 127 },
    { "filterEnvAttack", 38, 8, kAll, Kind::Knob, 127 },
    { "filterEnvDecay", 39, 9, kAll, Kind::Knob, 127 },
    { "filterEnvSustain", 40, 10, kAll, Kind::Knob, 127 },
    { "filterEnvRelease", 41, 11, kAll, Kind::Knob, 127 },
    { "filterResonance", 42, 4, kAll, Kind::Knob, 127 },
    { "filterEnvAmount", 43, 5, kAll, Kind::Knob, 127 },
    { "filterType", 44, 53, kAll, Kind::Stepped, 4 },
    { "filterVelocity", 45, 63, kAll, Kind::Switch, 1 },
    { "filterKeyboardTrack", 46, 55, kAll, Kind::Stepped, 3 },
    { "portamentoAuto", 65, 62, kAll, Kind::Switch, 1 },
    { "fmDepth", 70, 7, kAll, Kind::Knob, 127 },
    { "ampEnvRelease", 72, 15, kAll, Kind::Knob, 127 },
    { "ampEnvAttack", 73, 12, kAll, Kind::Knob, 127 },
    { "filterCutoff", 74, 3, kAll, Kind::Knob, 127 },
    { "osc2Semitones", 78, 0, kAll, Kind::Knob, 120 },
    { "pulseWidth", 79, 6, kAll, Kind::Knob, 127 },
    { "filterDistortion", 80, 52, 0x10, Kind::Switch, 1 },
};

CcParameter parameterWithKey(std::string_view key)
{
    for (const auto& info : ccParameters())
        if (info.key == key)
            return info.id;
    FAIL("no CC parameter with key " << key);
    return CcParameter::FilterCutoff;
}

int lowestBit(std::uint8_t bits)
{
    int shift = 0;
    while (((bits >> shift) & 1) == 0)
        ++shift;
    return shift;
}

int storedValue(const SimulatedNordLead2X::Program& program, const Expected& expected)
{
    const auto byte = program[expected.offset];
    if (expected.bits == kAll)
        return byte;
    return (static_cast<std::uint8_t>(byte) & expected.bits) >> lowestBit(expected.bits);
}

// The CC value the manual's conventions and HardwareAssumptions.h give for a value.
std::uint8_t expectedData(const Expected& expected, int value)
{
    if (expected.kind == Kind::Switch)
        return value != 0 ? hardware::kSwitchOnCcValue : 0;
    return static_cast<std::uint8_t>(value);
}

Bytes controlChange(MidiChannel channel, std::uint8_t cc, std::uint8_t data)
{
    return { static_cast<std::uint8_t>(0xB0 | channel), cc, data };
}

std::optional<int> engineValue(const EditorAndSynth& rig, Slot slot, CcParameter parameter)
{
    const auto& program = rig.engine.slotState(slot).program;
    if (! program)
        return std::nullopt;
    return ccParameterValue(*program, parameter);
}

// Sends nothing more and delivers everything the synth sent.
void settle(EditorAndSynth& rig)
{
    rig.runFor(5 * EditorAndSynth::kTickMs);
}

} // namespace

TEST_CASE("CC parameters: the table holds exactly the controls of the manual's CC chart")
{
    std::set<std::string_view> keys;
    for (const auto& info : ccParameters())
        keys.insert(info.key);
    std::set<std::string_view> expectedKeys;
    for (const auto& expected : kExpected)
        expectedKeys.insert(expected.key);

    CHECK(ccParameters().size() == kExpected.size());
    CHECK(keys == expectedKeys);
}

TEST_CASE("CC parameters: kinds, ranges and named steps")
{
    for (const auto& expected : kExpected) {
        CAPTURE(expected.key);
        const auto& info = ccParameterInfo(parameterWithKey(expected.key));
        CHECK(info.minValue == 0);
        CHECK(info.maxValue == expected.maxValue);
        switch (expected.kind) {
        case Kind::Knob:
            CHECK(info.kind == CcParameterKind::Knob);
            CHECK(info.choices.empty());
            break;
        case Kind::Stepped:
            CHECK(info.kind == CcParameterKind::Stepped);
            CHECK(info.choices.size() == static_cast<std::size_t>(expected.maxValue + 1));
            break;
        case Kind::Switch:
            CHECK(info.kind == CcParameterKind::Switch);
            break;
        }
    }
    CHECK(ccParameterInfo(CcParameter::RingModSync).choices[3] == "Sync + Ring Mod");
    CHECK(ccParameterInfo(CcParameter::Lfo2DestinationArpMode).choices[8] == "Off");
}

TEST_CASE("CC parameters: DAW parameters are named after the Slot and the manual's term, with stable IDs")
{
    CHECK(dawParameterName(Slot::B, CcParameter::Lfo1Rate) == "B: LFO1 Rate");
    CHECK(dawParameterId(Slot::A, CcParameter::FilterCutoff) == "A_filterCutoff");
    CHECK(dawParameterId(Slot::D, CcParameter::RingModSync) == "D_ringModSync");

    std::set<std::string> ids;
    for (auto slot : kAllSlots)
        for (const auto& info : ccParameters())
            ids.insert(dawParameterId(slot, info.id));
    CHECK(ids.size() == 4 * kExpected.size());
}

TEST_CASE("CC parameters: every parameter of every Slot goes out as its CC on the Slot's channel and changes the synth")
{
    const auto slot = GENERATE(Slot::A, Slot::B, Slot::C, Slot::D);
    EditorAndSynth rig;
    rig.connect();
    const auto channel = hardware::kDefaultSlotChannels[slotIndex(slot)];

    for (const auto& expected : kExpected) {
        const auto parameter = parameterWithKey(expected.key);
        for (int value : { 0, expected.maxValue / 2, expected.maxValue }) {
            CAPTURE(slotLetter(slot), expected.key, value);
            std::array<SimulatedNordLead2X::Program, 4> before;
            for (auto other : kAllSlots)
                before[slotIndex(other)] = rig.synth.editBuffer(other);
            rig.toSynth.clear();

            rig.engine.setParameter(slot, parameter, value);

            REQUIRE(rig.toSynth.messages().size() == 1);
            CHECK(rig.toSynth.messages().front() == controlChange(channel, expected.cc, expectedData(expected, value)));
            CHECK(storedValue(rig.synth.editBuffer(slot), expected) == value);
            CHECK(engineValue(rig, slot, parameter) == value);
            // Only this field of this Slot changed on the synth.
            for (auto other : kAllSlots) {
                auto expectedProgram = before[slotIndex(other)];
                if (other == slot)
                    expectedProgram[expected.offset] = rig.synth.editBuffer(slot)[expected.offset];
                CHECK(rig.synth.editBuffer(other) == expectedProgram);
            }
        }
    }
}

TEST_CASE("CC parameters: Ring Mod/Sync and Filter Distortion keep the other bits of byte 52")
{
    EditorAndSynth rig;
    auto program = EditorAndSynth::someProgram(9);
    // Sync on, distortion off, and bits 2, 3, 5, 6 and 7 set: unknown, and must survive.
    program[52] = static_cast<std::int8_t>(0b1110'1101);
    rig.synth.setEditBuffer(Slot::A, program);
    rig.connect();

    CHECK(engineValue(rig, Slot::A, CcParameter::RingModSync) == 1);
    CHECK(engineValue(rig, Slot::A, CcParameter::FilterDistortion) == 0);

    rig.engine.setParameter(Slot::A, CcParameter::RingModSync, 2);
    CHECK(static_cast<std::uint8_t>(rig.synth.editBuffer(Slot::A)[52]) == 0b1110'1110);
    rig.engine.setParameter(Slot::A, CcParameter::FilterDistortion, 1);
    CHECK(static_cast<std::uint8_t>(rig.synth.editBuffer(Slot::A)[52]) == 0b1111'1110);

    // The editor's copy keeps the same bits: written back as a dump, nothing is lost.
    REQUIRE(rig.engine.slotState(Slot::A).program);
    CHECK(static_cast<std::uint8_t>(rig.engine.slotState(Slot::A).program->value(ProgramParameter::SyncRingModDistortion))
          == 0b1111'1110);

    // Knob turned on the synth: the same bits are kept on both sides.
    rig.synth.turnPanelControl(Slot::A, 80, 0);
    rig.synth.turnPanelControl(Slot::A, 35, 3);
    settle(rig);
    CHECK(static_cast<std::uint8_t>(rig.synth.editBuffer(Slot::A)[52]) == 0b1110'1111);
    CHECK(static_cast<std::uint8_t>(rig.engine.slotState(Slot::A).program->value(ProgramParameter::SyncRingModDistortion))
          == 0b1110'1111);
}

TEST_CASE("CC parameters: turning any control on the synth updates that Slot in the editor and sends nothing back")
{
    const auto slot = GENERATE(Slot::A, Slot::B, Slot::C, Slot::D);
    EditorAndSynth rig;
    rig.connect();

    for (const auto& expected : kExpected) {
        const auto parameter = parameterWithKey(expected.key);
        for (int value : { 0, expected.maxValue, expected.maxValue / 2 }) {
            CAPTURE(slotLetter(slot), expected.key, value);
            rig.toSynth.clear();

            rig.synth.turnPanelControl(slot, expected.cc, expectedData(expected, value));
            settle(rig);

            CHECK(rig.toSynth.messages().empty());
            CHECK(engineValue(rig, slot, parameter) == value);
            CHECK(storedValue(rig.synth.editBuffer(slot), expected) == value);
        }
    }

    // Only the turned Slot changed in the editor: every other Slot still matches the synth.
    for (auto other : kAllSlots) {
        REQUIRE(rig.engine.slotState(other).program);
        CHECK(rig.engine.slotState(other).program->bytes() == rig.synth.editBuffer(other));
    }
}

TEST_CASE("CC parameters: a later read of the Slot shows the value set from either side")
{
    EditorAndSynth rig;
    rig.connect();

    rig.engine.setParameter(Slot::B, CcParameter::Lfo1Rate, 33);
    rig.synth.turnPanelControl(Slot::B, 44, 3); // Filter Type
    settle(rig);
    rig.engine.readSlot(Slot::B, rig.now);
    rig.runUntilSettled();

    CHECK(engineValue(rig, Slot::B, CcParameter::Lfo1Rate) == 33);
    CHECK(engineValue(rig, Slot::B, CcParameter::FilterType) == 3);
}

TEST_CASE("CC parameters: a CC on a shared channel updates every Slot on it")
{
    EditorAndSynth rig;
    rig.synth.setSlotChannel(Slot::C, hardware::kDefaultSlotChannels[slotIndex(Slot::A)]);
    rig.connect();
    rig.toSynth.clear();

    rig.synth.turnPanelControl(Slot::A, 19, 101); // LFO1 Rate
    settle(rig);

    CHECK(engineValue(rig, Slot::A, CcParameter::Lfo1Rate) == 101);
    CHECK(engineValue(rig, Slot::C, CcParameter::Lfo1Rate) == 101);
    CHECK(engineValue(rig, Slot::B, CcParameter::Lfo1Rate) != 101);
    CHECK(rig.toSynth.messages().empty());
    for (auto slot : kAllSlots)
        CHECK(rig.engine.slotState(slot).program->bytes() == rig.synth.editBuffer(slot));
}

TEST_CASE("CC parameters: a CC on the leftmost layered Slot's channel updates the whole Layer")
{
    EditorAndSynth rig;
    rig.synth.setLayered(Slot::B, true);
    rig.synth.setLayered(Slot::D, true);
    rig.connect();

    const std::array<std::uint8_t, 3> cutoff { static_cast<std::uint8_t>(0xB0 | hardware::kDefaultSlotChannels[1]), 74,
                                               17 };
    rig.fromSynth.send(cutoff);
    settle(rig);

    CHECK(engineValue(rig, Slot::B, CcParameter::FilterCutoff) == 17);
    CHECK(engineValue(rig, Slot::D, CcParameter::FilterCutoff) == 17);
    CHECK(engineValue(rig, Slot::A, CcParameter::FilterCutoff) != 17);
    CHECK(engineValue(rig, Slot::C, CcParameter::FilterCutoff) != 17);
}

TEST_CASE("CC parameters: the mod wheel, pedals and unknown controllers change nothing and are not echoed")
{
    EditorAndSynth rig;
    rig.connect();
    rig.toSynth.clear();
    std::array<Program, 4> before;
    for (auto slot : kAllSlots)
        before[slotIndex(slot)] = *rig.engine.slotState(slot).program;

    for (int cc : { 1, 11, 64, 32, 0, 2, 3, 9, 47, 66, 71, 75, 77, 81, 120, 127 })
        rig.fromSynth.send(controlChange(hardware::kDefaultSlotChannels[0], static_cast<std::uint8_t>(cc), 99));
    settle(rig);

    CHECK(rig.toSynth.messages().empty());
    for (auto slot : kAllSlots)
        CHECK(*rig.engine.slotState(slot).program == before[slotIndex(slot)]);
}

TEST_CASE("CC parameters: a CC for a Slot playing a Percussion Kit or on no Slot's channel is ignored")
{
    EditorAndSynth rig;
    rig.synth.playPercussionKit(Slot::A, {});
    rig.connect();
    const auto beforeB = *rig.engine.slotState(Slot::B).program;

    rig.fromSynth.send(controlChange(hardware::kDefaultSlotChannels[0], 74, 5));
    rig.fromSynth.send(controlChange(9, 74, 5));
    settle(rig);

    CHECK(rig.engine.slotState(Slot::A).content == SlotContent::PercussionKit);
    CHECK_FALSE(rig.engine.slotState(Slot::A).program);
    CHECK(*rig.engine.slotState(Slot::B).program == beforeB);
}

TEST_CASE("CC parameters: values outside the documented range are kept, from a read and from a CC")
{
    EditorAndSynth rig;
    auto program = EditorAndSynth::someProgram(2);
    program[59] = 7;   // Mod Wheel Destination, documented 0–4, found in [OFF-FB]
    program[0] = 125;  // Osc2 Semitones, documented 0–120
    program[60] = 3;   // Unison, documented 0–1
    rig.synth.setEditBuffer(Slot::A, program);
    rig.connect();

    CHECK(engineValue(rig, Slot::A, CcParameter::ModWheelDestination) == 7);
    CHECK(engineValue(rig, Slot::A, CcParameter::Osc2Semitones) == 125);
    CHECK(engineValue(rig, Slot::A, CcParameter::Unison) == 3);

    rig.synth.turnPanelControl(Slot::A, 18, 9);
    rig.synth.turnPanelControl(Slot::A, 78, 127);
    settle(rig);
    CHECK(engineValue(rig, Slot::A, CcParameter::ModWheelDestination) == 9);
    CHECK(engineValue(rig, Slot::A, CcParameter::Osc2Semitones) == 127);

    rig.engine.readSlot(Slot::A, rig.now);
    rig.runUntilSettled();
    CHECK(engineValue(rig, Slot::A, CcParameter::ModWheelDestination) == 9);
    CHECK(engineValue(rig, Slot::A, CcParameter::Osc2Semitones) == 127);
}

TEST_CASE("CC parameters: outgoing CC data stays within 0–127")
{
    EditorAndSynth rig;
    rig.connect();
    rig.toSynth.clear();

    rig.engine.setParameter(Slot::A, CcParameter::FilterCutoff, -5);
    rig.engine.setParameter(Slot::A, CcParameter::FilterResonance, 127);

    const auto channel = hardware::kDefaultSlotChannels[0];
    const std::vector<Bytes> expected { controlChange(channel, 74, 0), controlChange(channel, 42, 127) };
    CHECK(rig.toSynth.messages() == expected);
}

TEST_CASE("CC parameters: a switch reads any value from the threshold up as on")
{
    EditorAndSynth rig;
    rig.connect();

    for (std::uint8_t data : { std::uint8_t { 1 }, std::uint8_t { 64 }, std::uint8_t { 127 } }) {
        CAPTURE(data);
        rig.fromSynth.send(controlChange(hardware::kDefaultSlotChannels[0], 16, 0));
        settle(rig);
        CHECK(engineValue(rig, Slot::A, CcParameter::Unison) == 0);
        rig.fromSynth.send(controlChange(hardware::kDefaultSlotChannels[0], 16, data));
        settle(rig);
        CHECK(engineValue(rig, Slot::A, CcParameter::Unison) == 1);
    }
}

TEST_CASE("CC parameters: a Ring Mod/Sync CC value above 3 cannot be stored and is ignored")
{
    EditorAndSynth rig;
    rig.connect();
    const auto before = engineValue(rig, Slot::A, CcParameter::RingModSync);

    rig.fromSynth.send(controlChange(hardware::kDefaultSlotChannels[0], 35, 64));
    settle(rig);

    CHECK(engineValue(rig, Slot::A, CcParameter::RingModSync) == before);
}

TEST_CASE("CC parameters: before a Slot has been read, its CCs still go out")
{
    EditorAndSynth rig;

    rig.engine.setParameter(Slot::C, CcParameter::OctaveShift, 4);

    const std::vector<Bytes> expected { controlChange(hardware::kDefaultSlotChannels[2], 17, 4) };
    CHECK(rig.toSynth.messages() == expected);
    CHECK(rig.synth.editBuffer(Slot::C)[64] == 4);
}
