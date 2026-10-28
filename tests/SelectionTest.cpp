// Selection (#10): selecting stored Programs, Percussion Kits and Performances by Bank Select and
// Program Change, reading the Edit Buffers again afterwards, and following selections made on the
// synth. The engine is driven as the shell drives it, wired to the simulated synth; tests look at
// the MIDI it sends and its public state.

#include "EditorAndSynth.h"

#include "nl2x/HardwareAssumptions.h"
#include "nl2x/Performance.h"
#include "nl2x/Selection.h"
#include "nl2x/SysEx.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

using namespace nl2x;
using nl2x::test::EditorAndSynth;
using nl2x::test::SimulatedNordLead2X;

namespace {

using Bytes = std::vector<std::uint8_t>;
using Kind = ProgramSelection::Kind;

constexpr GlobalChannel kGlobal = hardware::kFactoryGlobalChannel;

Bytes bankSelect(MidiChannel channel, std::uint8_t value)
{
    return { static_cast<std::uint8_t>(0xB0 | channel), 32, value };
}

Bytes programChange(MidiChannel channel, std::uint8_t number)
{
    return { static_cast<std::uint8_t>(0xC0 | channel), number };
}

Program asProgram(const SimulatedNordLead2X::Program& values)
{
    return *Program::fromValues(values);
}

Program performanceProgram(const SimulatedNordLead2X::Performance& performance, Slot slot)
{
    const auto first = performance.begin() + static_cast<std::ptrdiff_t>(slotIndex(slot) * kProgramParameterCount);
    return *Program::fromValues(std::span<const std::int8_t>(first, kProgramParameterCount));
}

std::size_t count(const std::vector<Bytes>& messages, const Bytes& message)
{
    return static_cast<std::size_t>(std::count(messages.begin(), messages.end(), message));
}

// Connected, with the log cleared.
struct ConnectedRig : EditorAndSynth {
    ConnectedRig()
    {
        connect();
        toSynth.clear();
    }
};

// Long enough for the synth to settle and the Edit Buffers to be read again.
void settle(EditorAndSynth& rig)
{
    rig.runFor(hardware::kSelectionSettleMs + EditorAndSynth::kTickMs);
    rig.runUntilSettled();
}

} // namespace

TEST_CASE("Performance names run A0 to L9 without the letters I and K")
{
    CHECK(performanceName(0) == "A0");
    CHECK(performanceName(9) == "A9");
    CHECK(performanceName(10) == "B0");
    CHECK(performanceName(79) == "H9");
    CHECK(performanceName(80) == "J0");
    CHECK(performanceName(89) == "J9");
    CHECK(performanceName(90) == "L0");
    CHECK(performanceName(99) == "L9");
    CHECK(performanceName(-1).empty());
    CHECK(performanceName(100).empty());
    for (int number = 0; number < 100; ++number) {
        const auto name = performanceName(number);
        CHECK(name.size() == 2);
        CHECK(name.find_first_of("IK") == std::string::npos);
    }
}

TEST_CASE("Selecting a Program sends Bank Select then Program Change on the Slot's channel")
{
    struct Case {
        ProgramSelection selection;
        std::uint8_t bank;
        std::uint8_t program;
    };
    const std::vector<Case> cases {
        { { Kind::Program, 0, 1 }, 0, 0 },
        { { Kind::Program, 0, 99 }, 0, 98 },
        { { Kind::Program, 9, 1 }, 9, 0 },
        { { Kind::Program, 9, 99 }, 9, 98 },
        { { Kind::PercussionKit, 0, 0 }, 0, 99 },
        { { Kind::PercussionKit, 3, 9 }, 3, 108 },
    };
    for (const auto& c : cases) {
        ConnectedRig rig;
        rig.synth.setSlotChannel(Slot::C, 6);
        rig.engine.readPerformance(rig.now);
        rig.runUntilSettled();
        rig.toSynth.clear();

        CHECK(rig.engine.selectProgram(Slot::C, c.selection, rig.now) == SelectionResult::Sent);

        REQUIRE(rig.toSynth.messages().size() == 2);
        CHECK(rig.toSynth.messages()[0] == bankSelect(6, c.bank));
        CHECK(rig.toSynth.messages()[1] == programChange(6, c.program));
        CHECK(rig.engine.selection().programs[slotIndex(Slot::C)] == c.selection);
    }
}

TEST_CASE("Selections that do not exist on the synth are refused and send nothing")
{
    ConnectedRig rig;
    for (const auto& selection : { ProgramSelection { Kind::Program, 0, 0 }, ProgramSelection { Kind::Program, 0, 100 },
                                   ProgramSelection { Kind::Program, 10, 1 }, ProgramSelection { Kind::Program, -1, 1 },
                                   ProgramSelection { Kind::PercussionKit, 4, 0 },
                                   ProgramSelection { Kind::PercussionKit, 0, 10 } })
        CHECK(rig.engine.selectProgram(Slot::A, selection, rig.now) == SelectionResult::RefusedInvalid);
    for (const auto& selection : { PerformanceSelection { 0, 0 }, PerformanceSelection { 5, 0 },
                                   PerformanceSelection { 1, 100 } })
        CHECK(rig.engine.selectPerformance(selection, rig.now) == SelectionResult::RefusedInvalid);
    CHECK(rig.toSynth.messages().empty());
}

TEST_CASE("A Slot that is off, or on the Global Channel, cannot select a Program")
{
    EditorAndSynth rig;
    rig.synth.setSlotChannel(Slot::B, std::nullopt);
    rig.synth.setSlotChannel(Slot::C, kGlobal);
    rig.connect();
    rig.toSynth.clear();

    CHECK(rig.engine.selectProgram(Slot::B, { Kind::Program, 1, 5 }, rig.now) == SelectionResult::RefusedSlotChannelOff);
    CHECK(rig.engine.selectProgram(Slot::C, { Kind::Program, 1, 5 }, rig.now)
          == SelectionResult::RefusedSlotOnGlobalChannel);
    CHECK(rig.toSynth.messages().empty());
}

TEST_CASE("Selecting a Performance before the Global Channel is known is refused")
{
    EditorAndSynth rig;
    CHECK(rig.engine.selectPerformance({ 1, 0 }, rig.now) == SelectionResult::RefusedNoGlobalChannel);
    CHECK(rig.toSynth.messages().empty());
}

TEST_CASE("After selecting a Program the Slot is read again once the synth has settled")
{
    ConnectedRig rig;

    REQUIRE(rig.engine.selectProgram(Slot::B, { Kind::Program, 7, 42 }, rig.now) == SelectionResult::Sent);
    rig.runFor(hardware::kSelectionSettleMs - EditorAndSynth::kTickMs);
    // Nothing is asked while the synth loads the Program.
    CHECK(rig.toSynth.messages().size() == 2);

    settle(rig);
    CHECK(count(rig.toSynth.messages(), slotEditBufferRequest(kGlobal, Slot::B)) == 1);
    CHECK(count(rig.toSynth.messages(), slotEditBufferRequest(kGlobal, Slot::A)) == 0);
    CHECK(count(rig.toSynth.messages(), performanceEditBufferRequest(kGlobal)) == 1);
    CHECK(rig.engine.slotState(Slot::B).program == asProgram(rig.synth.bankProgram(7, 41)));
    CHECK(rig.engine.selection().programs[slotIndex(Slot::B)] == ProgramSelection { Kind::Program, 7, 42 });
}

TEST_CASE("After selecting a Percussion Kit the Slot shows that it plays a Kit")
{
    ConnectedRig rig;

    REQUIRE(rig.engine.selectProgram(Slot::A, { Kind::PercussionKit, 2, 5 }, rig.now) == SelectionResult::Sent);
    settle(rig);

    CHECK(rig.synth.playsPercussionKit(Slot::A));
    CHECK(rig.engine.slotState(Slot::A).content == SlotContent::PercussionKit);
}

TEST_CASE("A Program Change on a shared channel switches every Slot on it, and all of them are read again")
{
    EditorAndSynth rig;
    rig.synth.setSlotChannel(Slot::B, 0);
    rig.synth.setSlotChannel(Slot::D, 0);
    rig.connect();
    rig.toSynth.clear();

    REQUIRE(rig.engine.selectProgram(Slot::A, { Kind::Program, 4, 3 }, rig.now) == SelectionResult::Sent);
    settle(rig);

    const auto expected = asProgram(rig.synth.bankProgram(4, 2));
    for (auto slot : { Slot::A, Slot::B, Slot::D }) {
        CHECK(count(rig.toSynth.messages(), slotEditBufferRequest(kGlobal, slot)) == 1);
        CHECK(rig.engine.slotState(slot).program == expected);
        CHECK(rig.engine.selection().programs[slotIndex(slot)] == ProgramSelection { Kind::Program, 4, 3 });
    }
    CHECK(count(rig.toSynth.messages(), slotEditBufferRequest(kGlobal, Slot::C)) == 0);
    CHECK(rig.engine.slotState(Slot::C).program != expected);
}

TEST_CASE("Selections in quick succession end in one re-read")
{
    ConnectedRig rig;

    for (int number = 1; number <= 5; ++number) {
        REQUIRE(rig.engine.selectProgram(Slot::A, { Kind::Program, 1, number }, rig.now) == SelectionResult::Sent);
        rig.runFor(30);
    }
    settle(rig);

    CHECK(count(rig.toSynth.messages(), slotEditBufferRequest(kGlobal, Slot::A)) == 1);
    CHECK(rig.engine.slotState(Slot::A).program == asProgram(rig.synth.bankProgram(1, 4)));
}

TEST_CASE("Selecting a Performance sends Bank Select and Program Change on the Global Channel")
{
    struct Case {
        PerformanceSelection selection;
        std::uint8_t bank;
        std::uint8_t program;
    };
    for (const auto& c : { Case { { 1, 0 }, 0, 0 }, Case { { 4, 99 }, 3, 99 }, Case { { 2, 37 }, 1, 37 } }) {
        EditorAndSynth rig;
        rig.synth.setGlobalChannel(4);
        rig.connect();
        rig.toSynth.clear();

        CHECK(rig.engine.selectPerformance(c.selection, rig.now) == SelectionResult::Sent);

        REQUIRE(rig.toSynth.messages().size() == 2);
        CHECK(rig.toSynth.messages()[0] == bankSelect(4, c.bank));
        CHECK(rig.toSynth.messages()[1] == programChange(4, c.program));
        CHECK(rig.engine.selection().performance == c.selection);
    }
}

TEST_CASE("After a Performance change the Performance and all four Slots are read again, and CCs follow the new "
          "Slot Channels")
{
    ConnectedRig rig;
    auto stored = rig.synth.storedPerformance(3, 42);
    for (auto slot : kAllSlots)
        stored[kPerformanceSlotChannelOffset + slotIndex(slot)] = static_cast<std::int8_t>(9 + slotIndex(slot));
    rig.synth.setStoredPerformance(3, 42, stored);

    REQUIRE(rig.engine.selectPerformance({ 3, 42 }, rig.now) == SelectionResult::Sent);
    // Until the Performance is read, the Slots' old selections no longer apply.
    for (auto slot : kAllSlots)
        CHECK_FALSE(rig.engine.selection().programs[slotIndex(slot)]);
    settle(rig);

    CHECK(count(rig.toSynth.messages(), performanceEditBufferRequest(kGlobal)) == 1);
    for (auto slot : kAllSlots) {
        CHECK(count(rig.toSynth.messages(), slotEditBufferRequest(kGlobal, slot)) == 1);
        CHECK(rig.engine.slotState(slot).program == performanceProgram(stored, slot));
        CHECK(rig.engine.slotChannel(slot) == 9 + slotIndex(slot));
        // Taken from the Performance's pgmselect and bankselect: 42 = Program 43, Bank 3 − 1 = 2.
        CHECK(rig.engine.selection().programs[slotIndex(slot)] == ProgramSelection { Kind::Program, 2, 43 });
    }

    rig.toSynth.clear();
    rig.engine.setParameter(Slot::B, CcParameter::FilterCutoff, 77);
    REQUIRE(rig.toSynth.messages().size() == 1);
    CHECK(rig.toSynth.messages()[0] == Bytes { 0xB0 | 10, 74, 77 });
}

TEST_CASE("A Performance edit not sent yet is dropped when another Performance is selected")
{
    ConnectedRig rig;
    rig.toSynth.clear();
    REQUIRE(rig.engine.setPerformanceSetting(PerformanceSetting::SplitPoint, Slot::A, 72)
            == PerformanceEditResult::Accepted);
    REQUIRE(rig.engine.selectPerformance({ 3, 42 }, rig.now) == SelectionResult::Sent);
    settle(rig);
    rig.runFor(1000);

    for (const auto& message : rig.toSynth.messages())
        CHECK_FALSE(parsePerformanceDump(message));
}

TEST_CASE("A Program selected on the synth is followed: that Slot is read again")
{
    ConnectedRig rig;

    rig.synth.selectProgramOnPanel(Slot::B, 2, 10);
    settle(rig);

    CHECK(count(rig.toSynth.messages(), slotEditBufferRequest(kGlobal, Slot::B)) == 1);
    CHECK(count(rig.toSynth.messages(), slotEditBufferRequest(kGlobal, Slot::A)) == 0);
    CHECK(rig.engine.slotState(Slot::B).program == asProgram(rig.synth.bankProgram(2, 10)));
    CHECK(rig.engine.selection().programs[slotIndex(Slot::B)] == ProgramSelection { Kind::Program, 2, 11 });
}

TEST_CASE("A Program Change from the synth without Bank Select keeps the Slot's Bank")
{
    ConnectedRig rig;
    REQUIRE(rig.engine.selectProgram(Slot::A, { Kind::Program, 5, 1 }, rig.now) == SelectionResult::Sent);
    settle(rig);

    rig.pending.push_back(programChange(0, 20));
    settle(rig);

    CHECK(rig.engine.selection().programs[slotIndex(Slot::A)] == ProgramSelection { Kind::Program, 5, 21 });
}

TEST_CASE("A Performance selected on the synth is followed: everything is read again")
{
    ConnectedRig rig;
    auto stored = rig.synth.storedPerformance(2, 15);
    stored[kPerformanceSlotChannelOffset + slotIndex(Slot::A)] = 7;
    rig.synth.setStoredPerformance(2, 15, stored);

    rig.synth.selectPerformanceOnPanel(2, 15);
    settle(rig);

    CHECK(rig.engine.selection().performance == PerformanceSelection { 2, 15 });
    CHECK(count(rig.toSynth.messages(), performanceEditBufferRequest(kGlobal)) == 1);
    for (auto slot : kAllSlots) {
        CHECK(count(rig.toSynth.messages(), slotEditBufferRequest(kGlobal, slot)) == 1);
        CHECK(rig.engine.slotState(slot).program == performanceProgram(stored, slot));
    }
    CHECK(rig.engine.slotChannel(Slot::A) == 7);
}

TEST_CASE("A restored selection is shown, sends nothing, and survives the Performance read on connecting")
{
    EditorAndSynth rig;
    SelectionState saved;
    saved.programs[slotIndex(Slot::A)] = ProgramSelection { Kind::Program, 6, 12 };
    saved.programs[slotIndex(Slot::C)] = ProgramSelection { Kind::PercussionKit, 1, 3 };
    saved.performance = PerformanceSelection { 4, 80 };

    rig.engine.restoreSelection(saved);
    CHECK(rig.toSynth.messages().empty());
    CHECK(rig.engine.selection() == saved);

    rig.connect();
    for (const auto& message : rig.toSynth.messages())
        CHECK(message.front() == kSysExStart);
    CHECK(rig.engine.selection().programs[slotIndex(Slot::A)] == saved.programs[slotIndex(Slot::A)]);
    CHECK(rig.engine.selection().programs[slotIndex(Slot::C)] == saved.programs[slotIndex(Slot::C)]);
    CHECK(rig.engine.selection().performance == saved.performance);
    // Slots with nothing restored take what the Performance names (pgmselect 0, bankselect 0).
    CHECK(rig.engine.selection().programs[slotIndex(Slot::B)] == ProgramSelection { Kind::Program, 0, 1 });
}

TEST_CASE("Restoring ignores selections that do not exist")
{
    EditorAndSynth rig;
    SelectionState saved;
    saved.programs[0] = ProgramSelection { Kind::PercussionKit, 7, 0 };
    saved.performance = PerformanceSelection { 9, 0 };
    rig.engine.restoreSelection(saved);
    CHECK(rig.engine.selection() == SelectionState {});
}

TEST_CASE("Simulated synth: with P off, Program Changes are ignored")
{
    ConnectedRig rig;
    const auto before = rig.synth.editBuffer(Slot::A);
    rig.synth.setProgramChangeOn(false);

    REQUIRE(rig.engine.selectProgram(Slot::A, { Kind::Program, 3, 3 }, rig.now) == SelectionResult::Sent);
    settle(rig);

    CHECK(rig.synth.receivedProgramChangeCount() == 0);
    CHECK(rig.synth.editBuffer(Slot::A) == before);
}
