// A Program parameter changed in the editor reaches the synth as a CC on the Slot's channel.

#include "MidiCable.h"
#include "SimulatedNordLead2X.h"

#include "nl2x/Engine.h"
#include "nl2x/HardwareAssumptions.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cstdint>
#include <vector>

using namespace nl2x;
using nl2x::test::MidiCable;
using nl2x::test::SimulatedNordLead2X;

namespace {

// Decoded Program offset of Filter Cutoff [OFF-2X p.111].
constexpr std::size_t kCutoffOffset = 3;

struct EditorWiredToSynth {
    EditorWiredToSynth() { editorToSynth.plugInto([this](auto message) { synth.receive(message); }); }

    MidiCable editorToSynth;
    SimulatedNordLead2X synth;
    Engine engine { editorToSynth };
};

std::uint8_t controlChangeStatus(Slot slot)
{
    return static_cast<std::uint8_t>(0xB0 | hardware::kDefaultSlotChannels[slotIndex(slot)]);
}

} // namespace

TEST_CASE("Moving Slot A Filter Cutoff sends CC74 on Slot A's channel and changes the synth's cutoff")
{
    EditorWiredToSynth rig;

    rig.engine.setParameter(Slot::A, ProgramParameter::FilterCutoff, 100);

    const std::vector<std::vector<std::uint8_t>> expected { { controlChangeStatus(Slot::A), 74, 100 } };
    CHECK(rig.editorToSynth.messages() == expected);
    CHECK(rig.synth.editBuffer(Slot::A)[kCutoffOffset] == 100);
    CHECK(rig.synth.editBuffer(Slot::B)[kCutoffOffset] == 0);
}

TEST_CASE("Filter Cutoff of each Slot goes out on that Slot's channel only")
{
    const auto slot = GENERATE(Slot::A, Slot::B, Slot::C, Slot::D);
    EditorWiredToSynth rig;

    rig.engine.setParameter(slot, ProgramParameter::FilterCutoff, 42);

    REQUIRE(rig.editorToSynth.messages().size() == 1);
    CHECK(rig.editorToSynth.messages().front() == std::vector<std::uint8_t> { controlChangeStatus(slot), 74, 42 });
    for (auto other : kAllSlots)
        CHECK(rig.synth.editBuffer(other)[kCutoffOffset] == (other == slot ? 42 : 0));
}

TEST_CASE("Every movement of the knob is sent, including the ends of the range")
{
    EditorWiredToSynth rig;

    for (int value : { 0, 64, 127 })
        rig.engine.setParameter(Slot::A, ProgramParameter::FilterCutoff, value);

    const auto status = controlChangeStatus(Slot::A);
    const std::vector<std::vector<std::uint8_t>> expected { { status, 74, 0 }, { status, 74, 64 }, { status, 74, 127 } };
    CHECK(rig.editorToSynth.messages() == expected);
    CHECK(rig.synth.editBuffer(Slot::A)[kCutoffOffset] == 127);
}

TEST_CASE("DAW parameters are named after the Slot and the manual's term")
{
    CHECK(dawParameterName(Slot::A, ProgramParameter::FilterCutoff) == "A: Filter Cutoff");
    CHECK(dawParameterName(Slot::D, ProgramParameter::FilterCutoff) == "D: Filter Cutoff");
    CHECK(dawParameterId(Slot::A, ProgramParameter::FilterCutoff) == "A_filterCutoff");
}
