// Undo and redo (#14). The engine is driven as the shell drives it: the user's GUI edits are
// handed in between beginUserEdit and endUserEdit, DAW automation and the synth's CCs are not.
// Tests look at the MIDI crossing the boundary and at the simulated synth's Edit Buffers.

#include "EditorAndSynth.h"

#include "nl2x/HardwareAssumptions.h"
#include "nl2x/MorphAmounts.h"
#include "nl2x/Performance.h"
#include "nl2x/Program.h"
#include "nl2x/SysEx.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

using namespace nl2x;
using nl2x::test::EditorAndSynth;
using nl2x::test::SimulatedNordLead2X;

namespace {

using Bytes = std::vector<std::uint8_t>;

// Research §2–3: Filter Cutoff is CC74 and Program byte 3; Filter Resonance CC42, byte 4.
constexpr std::uint8_t kFilterCutoffCc = 74;
constexpr std::size_t kFilterCutoffOffset = 3;
constexpr std::size_t kFilterResonanceOffset = 4;
// Research §3: Pitch Bend Range is Performance offset 312, documented 0–8.
constexpr std::size_t kPitchBendRangeOffset = 312;

Bytes cutoffCc(MidiChannel channel, int value)
{
    return { static_cast<std::uint8_t>(0xB0 | channel), kFilterCutoffCc, static_cast<std::uint8_t>(value) };
}

// One GUI edit of a Slot's parameter, as one step of its own (a menu choice) or as part of `step`.
void userSetsParameter(EditorAndSynth& rig, EditStepId step, Slot slot, CcParameter parameter, int value)
{
    rig.engine.beginUserEdit(step);
    rig.engine.setParameter(slot, parameter, value);
    rig.engine.endUserEdit();
}

void userSetsPerformanceSetting(EditorAndSynth& rig, EditStepId step, PerformanceSetting setting, Slot slot, int value)
{
    rig.engine.beginUserEdit(step);
    rig.engine.setPerformanceSetting(setting, slot, value);
    rig.engine.endUserEdit();
}

int synthCutoff(const EditorAndSynth& rig, Slot slot)
{
    return rig.synth.editBuffer(slot)[kFilterCutoffOffset];
}

} // namespace

TEST_CASE("Undo of a CC edit sends the previous value on the Slot Channel, and redo the new one")
{
    EditorAndSynth rig;
    rig.connect();
    const auto channel = *rig.engine.slotChannel(Slot::B);
    const int original = synthCutoff(rig, Slot::B);
    const int edited = original == 100 ? 90 : 100;
    CHECK_FALSE(rig.engine.canUndo());

    userSetsParameter(rig, rig.engine.newEditStep(), Slot::B, CcParameter::FilterCutoff, edited);
    CHECK(synthCutoff(rig, Slot::B) == edited);
    REQUIRE(rig.engine.canUndo());
    CHECK_FALSE(rig.engine.canRedo());

    rig.toSynth.clear();
    rig.engine.undo(rig.now);
    CHECK(rig.toSynth.messages() == std::vector<Bytes> { cutoffCc(channel, original) });
    CHECK(synthCutoff(rig, Slot::B) == original);
    CHECK(ccParameterValue(*rig.engine.slotState(Slot::B).program, CcParameter::FilterCutoff) == original);
    CHECK_FALSE(rig.engine.canUndo());
    REQUIRE(rig.engine.canRedo());

    rig.toSynth.clear();
    rig.engine.redo(rig.now);
    CHECK(rig.toSynth.messages() == std::vector<Bytes> { cutoffCc(channel, edited) });
    CHECK(synthCutoff(rig, Slot::B) == edited);
    CHECK(rig.engine.canUndo());
    CHECK_FALSE(rig.engine.canRedo());
}

TEST_CASE("A knob drag of many values is one undo step")
{
    EditorAndSynth rig;
    rig.connect();
    const int original = synthCutoff(rig, Slot::A);

    const auto drag = rig.engine.newEditStep();
    // The drag spans timer ticks, with DAW automation of another parameter in between.
    for (int value : { 10, 20, 30, 40, 50 }) {
        userSetsParameter(rig, drag, Slot::A, CcParameter::FilterCutoff, value);
        rig.engine.setParameter(Slot::A, CcParameter::FilterResonance, value + 1);
        rig.tick();
    }

    rig.toSynth.clear();
    rig.engine.undo(rig.now);
    CHECK(rig.toSynth.messages() == std::vector<Bytes> { cutoffCc(*rig.engine.slotChannel(Slot::A), original) });
    CHECK(synthCutoff(rig, Slot::A) == original);
    // The automated parameter keeps the automation's value.
    CHECK(rig.synth.editBuffer(Slot::A)[kFilterResonanceOffset] == 51);
    CHECK_FALSE(rig.engine.canUndo());
}

TEST_CASE("Separate menu choices are separate steps, undone latest first")
{
    EditorAndSynth rig;
    rig.connect();
    const int original = synthCutoff(rig, Slot::A);

    userSetsParameter(rig, rig.engine.newEditStep(), Slot::A, CcParameter::FilterCutoff, 11);
    userSetsParameter(rig, rig.engine.newEditStep(), Slot::A, CcParameter::FilterCutoff, 22);

    rig.engine.undo(rig.now);
    CHECK(synthCutoff(rig, Slot::A) == 11);
    rig.engine.undo(rig.now);
    CHECK(synthCutoff(rig, Slot::A) == original);
    CHECK_FALSE(rig.engine.canUndo());
}

TEST_CASE("A drag that ends where it started leaves nothing to undo")
{
    EditorAndSynth rig;
    rig.connect();
    const int original = synthCutoff(rig, Slot::A);

    const auto drag = rig.engine.newEditStep();
    userSetsParameter(rig, drag, Slot::A, CcParameter::FilterCutoff, original == 0 ? 1 : 0);
    userSetsParameter(rig, drag, Slot::A, CcParameter::FilterCutoff, original);

    CHECK_FALSE(rig.engine.canUndo());
}

TEST_CASE("Undo of a Performance setting sends one coalesced Performance dump with the old value")
{
    EditorAndSynth rig;
    SimulatedNordLead2X::PerformanceSettings settings {};
    settings[353 - kPerformanceSlotChannelOffset] = 60;
    rig.synth.setPerformanceSettings(settings);
    rig.connect();

    const auto drag = rig.engine.newEditStep();
    for (int value : { 64, 70, 80 })
        userSetsPerformanceSetting(rig, drag, PerformanceSetting::SplitPoint, Slot::A, value);
    rig.runFor(1000);
    REQUIRE(rig.synth.performanceSettings()[353 - kPerformanceSlotChannelOffset] == 80);

    rig.toSynth.clear();
    rig.engine.undo(rig.now);
    rig.runFor(1000);
    const auto dumps = rig.performanceDumpsSent();
    REQUIRE(dumps.size() == 1);
    CHECK(rig.toSynth.messages().size() == 1);
    CHECK(parsePerformanceDump(dumps.front())->performance.setting(PerformanceSetting::SplitPoint) == 60);
    CHECK(rig.synth.performanceSettings()[353 - kPerformanceSlotChannelOffset] == 60);

    rig.toSynth.clear();
    rig.engine.redo(rig.now);
    rig.runFor(1000);
    REQUIRE(rig.performanceDumpsSent().size() == 1);
    CHECK(rig.synth.performanceSettings()[353 - kPerformanceSlotChannelOffset] == 80);
}

TEST_CASE("Undo puts back a Performance value outside the documented range exactly as it was read")
{
    EditorAndSynth rig;
    SimulatedNordLead2X::PerformanceSettings settings {};
    settings[kPitchBendRangeOffset - kPerformanceSlotChannelOffset] = 14;
    rig.synth.setPerformanceSettings(settings);
    rig.connect();

    userSetsPerformanceSetting(rig, rig.engine.newEditStep(), PerformanceSetting::PitchBendRange, Slot::A, 2);
    rig.runFor(500);
    rig.engine.undo(rig.now);
    rig.runFor(500);

    CHECK(rig.synth.performanceSettings()[kPitchBendRangeOffset - kPerformanceSlotChannelOffset] == 14);
}

TEST_CASE("Changes that are not the user's edits in the plugin are not undoable")
{
    EditorAndSynth rig;
    rig.connect();

    SECTION("a knob turned on the synth")
    {
        rig.synth.turnPanelControl(Slot::A, kFilterCutoffCc, 33);
        rig.runFor(100);
        REQUIRE(ccParameterValue(*rig.engine.slotState(Slot::A).program, CcParameter::FilterCutoff) == 33);
    }
    SECTION("DAW automation")
    {
        rig.engine.setParameter(Slot::A, CcParameter::FilterCutoff, 33);
        rig.engine.setPerformanceSetting(PerformanceSetting::SplitPoint, Slot::A, 33);
        rig.runFor(500);
    }

    CHECK_FALSE(rig.engine.canUndo());
    rig.toSynth.clear();
    rig.engine.undo(rig.now);
    rig.runFor(500);
    CHECK(rig.toSynth.messages().empty());
    CHECK(synthCutoff(rig, Slot::A) == 33);
}

TEST_CASE("Undo after the synth moved the same knob restores the value from before the user's edit")
{
    EditorAndSynth rig;
    rig.connect();
    const int original = synthCutoff(rig, Slot::A);

    userSetsParameter(rig, rig.engine.newEditStep(), Slot::A, CcParameter::FilterCutoff, 50);
    rig.synth.turnPanelControl(Slot::A, kFilterCutoffCc, 70);
    rig.runFor(100);
    rig.engine.undo(rig.now);

    CHECK(synthCutoff(rig, Slot::A) == original);
}

TEST_CASE("A new edit after an undo empties redo")
{
    EditorAndSynth rig;
    rig.connect();

    userSetsParameter(rig, rig.engine.newEditStep(), Slot::A, CcParameter::FilterCutoff, 11);
    rig.engine.undo(rig.now);
    REQUIRE(rig.engine.canRedo());

    userSetsParameter(rig, rig.engine.newEditStep(), Slot::A, CcParameter::FilterCutoff, 22);
    CHECK_FALSE(rig.engine.canRedo());
    rig.toSynth.clear();
    rig.engine.redo(rig.now);
    CHECK(rig.toSynth.messages().empty());
    CHECK(synthCutoff(rig, Slot::A) == 22);
}

TEST_CASE("A drag continued after an undo is a new step")
{
    EditorAndSynth rig;
    rig.connect();
    const auto drag = rig.engine.newEditStep();
    userSetsParameter(rig, drag, Slot::A, CcParameter::FilterCutoff, 11);
    rig.engine.undo(rig.now);
    userSetsParameter(rig, drag, Slot::A, CcParameter::FilterCutoff, 22);

    CHECK_FALSE(rig.engine.canRedo());
    rig.engine.undo(rig.now);
    CHECK(synthCutoff(rig, Slot::A) != 22);
    CHECK_FALSE(rig.engine.canUndo());
}

TEST_CASE("The history keeps the latest 200 steps")
{
    EditorAndSynth rig;
    rig.connect();

    // Values 1 … 250, each its own step: undo can go back to 50, the value before step 51.
    for (int value = 1; value <= 250; ++value)
        userSetsParameter(rig, rig.engine.newEditStep(), Slot::A, CcParameter::FilterCutoff, value % 128);

    int undone = 0;
    while (rig.engine.canUndo()) {
        rig.engine.undo(rig.now);
        ++undone;
    }
    CHECK(undone == 200);
    CHECK(synthCutoff(rig, Slot::A) == 50);
}

TEST_CASE("When the synth's Edit Buffer replaces the plugin's copy, its edits leave the history")
{
    EditorAndSynth rig;
    rig.connect();

    userSetsParameter(rig, rig.engine.newEditStep(), Slot::A, CcParameter::FilterCutoff, 11);
    userSetsParameter(rig, rig.engine.newEditStep(), Slot::B, CcParameter::FilterCutoff, 22);
    userSetsPerformanceSetting(rig, rig.engine.newEditStep(), PerformanceSetting::SplitPoint, Slot::A, 40);
    rig.runFor(500);

    SECTION("a read that finds other values")
    {
        // A change on the synth that sends no CC (here: a new Program in Slot B's Edit Buffer).
        rig.synth.setEditBuffer(Slot::B, EditorAndSynth::someProgram(9));
        rig.engine.readSlot(Slot::B, rig.now);
        rig.runUntilSettled();

        // Only Slot A's and the Performance's edits are left.
        rig.engine.undo(rig.now);
        rig.runFor(500);
        CHECK(rig.synth.performanceSettings()[353 - kPerformanceSlotChannelOffset] != 40);
        rig.toSynth.clear();
        rig.engine.undo(rig.now);
        CHECK(rig.toSynth.messages().size() == 1);
        CHECK(synthCutoff(rig, Slot::A) != 11);
        CHECK_FALSE(rig.engine.canUndo());
        CHECK(synthCutoff(rig, Slot::B) == EditorAndSynth::someProgram(9)[kFilterCutoffOffset]);
    }
    SECTION("a read that finds the same values keeps them")
    {
        rig.engine.readSlot(Slot::B, rig.now);
        rig.engine.readPerformance(rig.now);
        rig.runUntilSettled();
        int undone = 0;
        while (rig.engine.canUndo()) {
            rig.engine.undo(rig.now);
            ++undone;
        }
        CHECK(undone == 3);
    }
    SECTION("connecting again")
    {
        rig.connect();
        CHECK_FALSE(rig.engine.canUndo());
    }
}

namespace {

// Research §3: Filter Cutoff's Morph Amount is Program byte 28.
constexpr std::size_t kFilterCutoffMorphOffset = 28;

void userSetsMorphAmount(EditorAndSynth& rig, EditStepId step, Slot slot, ProgramParameter base, int amount)
{
    rig.engine.beginUserEdit(step);
    rig.engine.setMorphAmount(slot, base, amount, rig.now);
    rig.engine.endUserEdit();
}

// The Filter Cutoff Morph Amount in each Program dump sent to `slot`'s Edit Buffer, in order.
std::vector<int> cutoffMorphAmountsSent(const EditorAndSynth& rig, Slot slot)
{
    std::vector<int> amounts;
    for (const auto& message : rig.programDumpsSent()) {
        const auto dump = parseProgramDump(message);
        if (dump && dump->spec == slotIndex(slot))
            amounts.push_back(dump->program.bytes()[kFilterCutoffMorphOffset]);
    }
    return amounts;
}

} // namespace

TEST_CASE("Undo and redo of a Morph Amount send Program dumps with the old and the new amount, throttled")
{
    EditorAndSynth rig;
    rig.connect();
    const int original = rig.synth.editBuffer(Slot::C)[kFilterCutoffMorphOffset];
    const int edited = original == 40 ? 30 : 40;

    userSetsMorphAmount(rig, rig.engine.newEditStep(), Slot::C, ProgramParameter::FilterCutoff, edited);
    rig.runFor(500);
    REQUIRE(rig.synth.editBuffer(Slot::C)[kFilterCutoffMorphOffset] == edited);

    rig.toSynth.clear();
    rig.sendTimes.clear();
    rig.engine.undo(rig.now);
    rig.engine.redo(rig.now);
    rig.engine.undo(rig.now);
    rig.runFor(1000);

    // Three changes within one throttle interval: the last one always arrives, and dumps keep
    // their interval.
    const auto amounts = cutoffMorphAmountsSent(rig, Slot::C);
    REQUIRE_FALSE(amounts.empty());
    CHECK(amounts.size() <= 2);
    CHECK(amounts.back() == original);
    CHECK(rig.synth.editBuffer(Slot::C)[kFilterCutoffMorphOffset] == original);
    for (std::size_t i = 1; i < rig.sendTimes.size(); ++i)
        CHECK(rig.sendTimes[i] - rig.sendTimes[i - 1] >= hardware::kProgramDumpIntervalMs);

    rig.toSynth.clear();
    rig.engine.redo(rig.now);
    rig.runFor(1000);
    CHECK(cutoffMorphAmountsSent(rig, Slot::C) == std::vector<int> { edited });
    CHECK(rig.synth.editBuffer(Slot::C)[kFilterCutoffMorphOffset] == edited);
}

TEST_CASE("A Morph Amount drag is one undo step")
{
    EditorAndSynth rig;
    rig.connect();
    const int original = rig.synth.editBuffer(Slot::A)[kFilterCutoffMorphOffset];

    const auto drag = rig.engine.newEditStep();
    for (int amount = -60; amount <= 60; amount += 20) {
        userSetsMorphAmount(rig, drag, Slot::A, ProgramParameter::FilterCutoff, amount);
        rig.tick();
    }
    rig.runFor(500);

    rig.engine.undo(rig.now);
    rig.runFor(500);
    CHECK(rig.synth.editBuffer(Slot::A)[kFilterCutoffMorphOffset] == original);
    CHECK_FALSE(rig.engine.canUndo());
}

TEST_CASE("Steps mixing CC parameters and Morph Amounts undo latest first")
{
    EditorAndSynth rig;
    rig.connect();
    const int originalCutoff = synthCutoff(rig, Slot::A);
    const int originalMorph = rig.synth.editBuffer(Slot::A)[kFilterCutoffMorphOffset];

    userSetsParameter(rig, rig.engine.newEditStep(), Slot::A, CcParameter::FilterCutoff, 11);
    userSetsMorphAmount(rig, rig.engine.newEditStep(), Slot::A, ProgramParameter::FilterCutoff, 55);
    userSetsParameter(rig, rig.engine.newEditStep(), Slot::A, CcParameter::FilterCutoff, 22);
    rig.runFor(500);

    rig.engine.undo(rig.now);
    rig.runFor(500);
    CHECK(synthCutoff(rig, Slot::A) == 11);
    CHECK(rig.synth.editBuffer(Slot::A)[kFilterCutoffMorphOffset] == 55);

    rig.engine.undo(rig.now);
    rig.runFor(500);
    CHECK(synthCutoff(rig, Slot::A) == 11);
    CHECK(rig.synth.editBuffer(Slot::A)[kFilterCutoffMorphOffset] == originalMorph);

    rig.engine.undo(rig.now);
    rig.runFor(500);
    CHECK(synthCutoff(rig, Slot::A) == originalCutoff);
    CHECK_FALSE(rig.engine.canUndo());
}

TEST_CASE("Morph Amounts set by DAW automation are not undoable")
{
    EditorAndSynth rig;
    rig.connect();
    rig.engine.setMorphAmount(Slot::A, ProgramParameter::FilterCutoff, 33, rig.now);
    CHECK_FALSE(rig.engine.canUndo());
}
