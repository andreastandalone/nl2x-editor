// Morph Amounts and the parameters without a CC (#7). The engine is driven as the shell drives it,
// wired to the simulated synth; tests look at the Program dumps crossing the MIDI boundary and at
// what the synth received. Offsets are written from research §3 on their own: Morph Amounts are
// offsets 25–49, in the order of their base parameters 0–24; byte 52 holds sync (bit 0), ring mod
// (bit 1) and Filter Distortion (bit 4).

#include "EditorAndSynth.h"

#include "nl2x/HardwareAssumptions.h"
#include "nl2x/MorphAmounts.h"
#include "nl2x/Program.h"
#include "nl2x/SysEx.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstdint>
#include <set>
#include <string>
#include <vector>

using namespace nl2x;
using nl2x::test::EditorAndSynth;
using nl2x::test::SimulatedNordLead2X;
using Catch::Matchers::ContainsSubstring;

namespace {

using Bytes = std::vector<std::uint8_t>;

constexpr std::size_t kFilterCutoffOffset = 3;
constexpr std::size_t kFilterCutoffMorphOffset = 28;
constexpr std::size_t kLfo2AmountMorphOffset = 49;
constexpr std::size_t kByte52 = 52;
constexpr std::uint8_t kRingModSyncCc = 35;

// The decoded Program of a type-0 dump, with its spec.
struct SentDump {
    std::uint8_t type;
    std::uint8_t spec;
    SimulatedNordLead2X::Program values;
};

std::vector<SentDump> programDumps(const EditorAndSynth& rig)
{
    std::vector<SentDump> dumps;
    for (const auto& message : rig.programDumpsSent()) {
        const auto parsed = parseSysEx(message);
        REQUIRE(parsed.values);
        SentDump dump { parsed.type, parsed.spec, {} };
        std::copy(parsed.values->begin(), parsed.values->end(), dump.values.begin());
        dumps.push_back(dump);
    }
    return dumps;
}

std::vector<SentDump> programDumpsTo(const EditorAndSynth& rig, Slot slot)
{
    std::vector<SentDump> dumps;
    for (const auto& dump : programDumps(rig))
        if (dump.spec == slotIndex(slot))
            dumps.push_back(dump);
    return dumps;
}

std::vector<Bytes> controlChanges(const EditorAndSynth& rig, std::uint8_t cc)
{
    std::vector<Bytes> found;
    for (const auto& message : rig.toSynth.messages())
        if (message.size() == 3 && (message[0] & 0xF0) == 0xB0 && message[1] == cc)
            found.push_back(message);
    return found;
}

// Every offset where two Programs differ.
std::set<std::size_t> differences(const SimulatedNordLead2X::Program& a, const SimulatedNordLead2X::Program& b)
{
    std::set<std::size_t> offsets;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i] != b[i])
            offsets.insert(i);
    return offsets;
}

void checkNoBankDumps(const EditorAndSynth& rig)
{
    CHECK(rig.synth.receivedDumpCount(SimulatedNordLead2X::DumpDestination::Bank) == 0);
    for (const auto& dump : programDumps(rig))
        CHECK(dump.type == hardware::kProgramEditBufferDumpType);
}

void checkSpacing(const std::vector<TimeMs>& times)
{
    for (std::size_t i = 1; i < times.size(); ++i)
        CHECK(times[i] - times[i - 1] >= hardware::kProgramDumpIntervalMs);
}

} // namespace

TEST_CASE("A Morph Amount edit goes to the Slot's Edit Buffer as a full Program dump differing only at its offset")
{
    EditorAndSynth rig;
    rig.connect();
    const auto before = rig.synth.editBuffer(Slot::B);
    rig.toSynth.clear();

    CHECK(rig.engine.setMorphAmount(Slot::B, ProgramParameter::FilterCutoff, -42, rig.now) == MorphEditResult::Accepted);
    rig.runFor(200);

    const auto dumps = programDumps(rig);
    REQUIRE(dumps.size() == 1);
    CHECK(dumps[0].type == hardware::kProgramEditBufferDumpType);
    CHECK(dumps[0].spec == 1);
    CHECK(differences(dumps[0].values, before) == std::set<std::size_t> { kFilterCutoffMorphOffset });
    CHECK(dumps[0].values[kFilterCutoffMorphOffset] == -42);
    CHECK(rig.synth.editBuffer(Slot::B)[kFilterCutoffMorphOffset] == -42);
    // Nothing else: no CC for a Morph Amount (research §2).
    CHECK(rig.toSynth.messages().size() == 1);
    checkNoBankDumps(rig);
}

TEST_CASE("The first Morph edit goes out at once")
{
    EditorAndSynth rig;
    rig.connect();
    rig.runFor(100);
    rig.toSynth.clear();

    rig.engine.setMorphAmount(Slot::A, ProgramParameter::Lfo2Amount, 5, rig.now);

    REQUIRE(rig.programDumpsSent().size() == 1);
    CHECK(programDumps(rig)[0].values[kLfo2AmountMorphOffset] == 5);
}

TEST_CASE("A Program dump carries CC edits and bytes outside their documented range unchanged")
{
    EditorAndSynth rig;
    auto program = EditorAndSynth::someProgram(9);
    program[0] = 125;                                  // Osc2 Semitones above 120
    program[kByte52] = static_cast<std::int8_t>(0xEF); // unknown bits of byte 52 set
    program[65] = -7;                                  // LFO2 Dest/Arp Mode below 0
    rig.synth.setEditBuffer(Slot::A, program);
    rig.connect();

    rig.engine.setParameter(Slot::A, CcParameter::FilterCutoff, 11);
    rig.engine.setMorphAmount(Slot::A, ProgramParameter::FilterCutoff, 100, rig.now);
    rig.runFor(200);

    const auto dumps = programDumpsTo(rig, Slot::A);
    REQUIRE(dumps.size() == 1);
    auto expected = program;
    expected[kFilterCutoffOffset] = 11;
    expected[kFilterCutoffMorphOffset] = 100;
    CHECK(dumps[0].values == expected);
    CHECK(rig.synth.editBuffer(Slot::A) == expected);
}

TEST_CASE("A user Morph Amount outside -128 to 127 is brought into it")
{
    EditorAndSynth rig;
    rig.connect();

    rig.engine.setMorphAmount(Slot::A, ProgramParameter::Gain, 300, rig.now);
    rig.runFor(200);
    rig.engine.setMorphAmount(Slot::A, ProgramParameter::Gain, -300, rig.now);
    rig.runFor(200);

    const auto dumps = programDumpsTo(rig, Slot::A);
    REQUIRE(dumps.size() == 2);
    CHECK(dumps[0].values[25 + 17] == 127);
    CHECK(dumps[1].values[25 + 17] == -128);
}

TEST_CASE("A burst of Morph edits is coalesced to about 10 dumps per second, the last carrying the final value")
{
    EditorAndSynth rig;
    rig.connect();
    // Past the gap after the last read, so that the first edit can go out at once.
    rig.runFor(100);
    rig.toSynth.clear();
    const auto start = rig.now;

    // 100 edits over one second, one per 10 ms tick, as a dragged knob or DAW automation gives.
    for (int i = 0; i < 100; ++i) {
        rig.engine.setMorphAmount(Slot::C, ProgramParameter::FilterResonance, i - 50, rig.now);
        rig.tick();
    }
    rig.runFor(500);

    const auto times = rig.synth.programEditBufferDumpTimes(Slot::C);
    CHECK(times.size() <= 11);
    CHECK(times.size() >= 9);
    checkSpacing(times);
    CHECK(times.front() == start);
    const auto dumps = programDumpsTo(rig, Slot::C);
    REQUIRE_FALSE(dumps.empty());
    CHECK(dumps.back().values[25 + 4] == 49);
    CHECK(rig.synth.editBuffer(Slot::C)[25 + 4] == 49);
    checkNoBankDumps(rig);
}

TEST_CASE("Each Slot is throttled on its own")
{
    EditorAndSynth rig;
    rig.connect();
    rig.toSynth.clear();

    for (int i = 0; i < 50; ++i) {
        rig.engine.setMorphAmount(Slot::A, ProgramParameter::OscMix, i, rig.now);
        rig.engine.setMorphAmount(Slot::D, ProgramParameter::OscMix, -i, rig.now);
        rig.tick();
    }
    rig.runFor(500);

    const auto timesA = rig.synth.programEditBufferDumpTimes(Slot::A);
    const auto timesD = rig.synth.programEditBufferDumpTimes(Slot::D);
    checkSpacing(timesA);
    checkSpacing(timesD);
    // Each gets its own budget: D is not starved by A, and goes out right after it (one request gap).
    CHECK(timesA.size() >= 5);
    CHECK(timesD.size() >= 5);
    CHECK(timesD.front() - timesA.front() <= hardware::kRequestSpacingMs + EditorAndSynth::kTickMs);
    CHECK(programDumpsTo(rig, Slot::A).back().values[25 + 2] == 49);
    CHECK(programDumpsTo(rig, Slot::D).back().values[25 + 2] == -49);
    CHECK(programDumpsTo(rig, Slot::B).empty());
}

TEST_CASE("No Program dump goes out while a request waits for its reply; it follows the reply")
{
    EditorAndSynth rig;
    rig.connect();
    rig.runFor(100);
    // The synth stops answering, so the read waits for its whole timeout.
    rig.synth.setAnswersSysEx(false);
    rig.engine.readSlot(Slot::B, rig.now);
    const auto requested = rig.now;
    rig.toSynth.clear();

    rig.engine.setMorphAmount(Slot::A, ProgramParameter::PulseWidth, 33, rig.now);
    rig.runFor(hardware::kRequestReplyTimeoutMs - 20);
    CHECK(rig.programDumpsSent().empty());

    rig.runFor(200);
    REQUIRE(rig.programDumpsSent().size() == 1);
    // After the timeout and the gap that follows every request.
    CHECK(rig.sendTimes.back() >= requested + hardware::kRequestReplyTimeoutMs + hardware::kRequestSpacingMs);
    CHECK(programDumps(rig)[0].values[25 + 6] == 33);
}

TEST_CASE("No Program dump goes out while requests are suspended; it follows on resuming")
{
    EditorAndSynth rig;
    rig.connect();
    rig.engine.setRequestsSuspended(true, rig.now);
    rig.toSynth.clear();

    rig.engine.setMorphAmount(Slot::A, ProgramParameter::FmDepth, 7, rig.now);
    rig.runFor(500);
    CHECK(rig.programDumpsSent().empty());

    rig.engine.setRequestsSuspended(false, rig.now);
    rig.runFor(100);
    REQUIRE(rig.programDumpsSent().size() == 1);
    CHECK(rig.synth.editBuffer(Slot::A)[25 + 7] == 7);
}

TEST_CASE("A Morph edit not sent yet survives a read of the Slot")
{
    EditorAndSynth rig;
    rig.connect();
    rig.engine.setMorphAmount(Slot::A, ProgramParameter::AmpEnvAttack, 1, rig.now);
    // Within the interval: the next edit waits, and a read goes out first.
    rig.engine.readSlot(Slot::A, rig.now);
    rig.engine.setMorphAmount(Slot::A, ProgramParameter::AmpEnvAttack, 2, rig.now);
    rig.runUntilSettled();
    rig.runFor(200);

    REQUIRE(rig.engine.slotState(Slot::A).program);
    CHECK(rig.engine.slotState(Slot::A).program->value(ProgramParameter::AmpEnvAttackMorphAmount) == 2);
    CHECK(rig.synth.editBuffer(Slot::A)[25 + 12] == 2);
}

TEST_CASE("A selection drops Morph edits not sent yet, so they never land on the new Program")
{
    EditorAndSynth rig;
    rig.connect();
    rig.engine.setMorphAmount(Slot::A, ProgramParameter::Gain, 10, rig.now);
    rig.engine.setMorphAmount(Slot::A, ProgramParameter::Gain, 20, rig.now);
    rig.engine.selectProgram(Slot::A, ProgramSelection { ProgramSelection::Kind::Program, 1, 5 }, rig.now);
    rig.toSynth.clear();

    rig.runFor(1000);

    CHECK(programDumpsTo(rig, Slot::A).empty());
    CHECK(rig.synth.editBuffer(Slot::A) == rig.synth.bankProgram(1, 4));
}

TEST_CASE("Morph edits are refused on a Slot playing a Percussion Kit or not read yet")
{
    SECTION("Percussion Kit")
    {
        EditorAndSynth rig;
        rig.synth.playPercussionKit(Slot::C, {});
        rig.connect();
        rig.toSynth.clear();

        const auto result = rig.engine.setMorphAmount(Slot::C, ProgramParameter::FilterCutoff, 10, rig.now);
        rig.runFor(300);

        CHECK(result == MorphEditResult::RefusedSlotPlaysPercussionKit);
        CHECK_THAT(morphEditRefusalText(result, Slot::C), ContainsSubstring("Slot C is playing a Percussion Kit"));
        CHECK(rig.toSynth.messages().empty());
    }
    SECTION("not read")
    {
        EditorAndSynth rig;
        const auto result = rig.engine.setMorphAmount(Slot::A, ProgramParameter::FilterCutoff, 10, rig.now);
        rig.runFor(300);

        CHECK(result == MorphEditResult::RefusedSlotNotRead);
        CHECK_THAT(morphEditRefusalText(result, Slot::A), ContainsSubstring("has not been read"));
        CHECK(rig.toSynth.messages().empty());
    }
    SECTION("not morphable")
    {
        EditorAndSynth rig;
        rig.connect();
        CHECK(rig.engine.setMorphAmount(Slot::A, ProgramParameter::FilterType, 10, rig.now)
              == MorphEditResult::RefusedNotMorphable);
        CHECK(rig.engine.setMorphAmount(Slot::A, ProgramParameter::FilterCutoffMorphAmount, 10, rig.now)
              == MorphEditResult::RefusedNotMorphable);
    }
}

TEST_CASE("Ring Mod/Sync goes out by CC35 alone while CC35 reaches the ring-mod bit")
{
    REQUIRE(hardware::Switches {}.ringModReachableByCc35 == hardware::kRingModReachableByCc35);
    EditorAndSynth rig(hardware::Switches { true });
    rig.connect();
    rig.toSynth.clear();

    rig.engine.setParameter(Slot::A, CcParameter::RingModSync, 2);
    rig.runFor(300);

    CHECK(controlChanges(rig, kRingModSyncCc) == std::vector<Bytes> { { 0xB0, kRingModSyncCc, 2 } });
    CHECK(rig.programDumpsSent().empty());
    CHECK((rig.synth.editBuffer(Slot::A)[kByte52] & 0x03) == 2);
}

TEST_CASE("When CC35 does not reach the ring-mod bit, Ring Mod goes out as a Program dump and Sync by CC35")
{
    EditorAndSynth rig(hardware::Switches { false });
    auto program = EditorAndSynth::someProgram(3);
    program[kByte52] = 0x10; // Distortion on, Sync and Ring Mod off
    rig.synth.setEditBuffer(Slot::B, program);
    rig.connect();
    rig.toSynth.clear();

    SECTION("Ring Mod on: a dump, no CC")
    {
        rig.engine.setParameter(Slot::B, CcParameter::RingModSync, 2);
        rig.runFor(300);

        CHECK(controlChanges(rig, kRingModSyncCc).empty());
        const auto dumps = programDumpsTo(rig, Slot::B);
        REQUIRE(dumps.size() == 1);
        CHECK(differences(dumps[0].values, program) == std::set<std::size_t> { kByte52 });
        CHECK(rig.synth.editBuffer(Slot::B)[kByte52] == 0x12);
        checkNoBankDumps(rig);
    }
    SECTION("Sync on, Ring Mod unchanged: CC35 = 1, no dump")
    {
        rig.engine.setParameter(Slot::B, CcParameter::RingModSync, 1);
        rig.runFor(300);

        CHECK(controlChanges(rig, kRingModSyncCc) == std::vector<Bytes> { { 0xB1, kRingModSyncCc, 1 } });
        CHECK(rig.programDumpsSent().empty());
        CHECK(rig.synth.editBuffer(Slot::B)[kByte52] == 0x11);
    }
    SECTION("Both on: CC35 for Sync and a dump for Ring Mod")
    {
        rig.engine.setParameter(Slot::B, CcParameter::RingModSync, 3);
        rig.runFor(300);

        CHECK(controlChanges(rig, kRingModSyncCc) == std::vector<Bytes> { { 0xB1, kRingModSyncCc, 1 } });
        CHECK(programDumpsTo(rig, Slot::B).size() == 1);
        CHECK(rig.synth.editBuffer(Slot::B)[kByte52] == 0x13);
    }
    SECTION("The synth's Sync button changes only the sync bit")
    {
        rig.engine.setParameter(Slot::B, CcParameter::RingModSync, 2);
        rig.runFor(300);
        rig.synth.turnPanelControl(Slot::B, kRingModSyncCc, 1);
        rig.runFor(50);

        REQUIRE(rig.engine.slotState(Slot::B).program);
        CHECK(ccParameterValue(*rig.engine.slotState(Slot::B).program, CcParameter::RingModSync) == 3);
        CHECK(rig.synth.editBuffer(Slot::B)[kByte52] == 0x13);
    }
}

TEST_CASE("Morph Amounts have stable DAW identities, one per morphable parameter and Slot")
{
    CHECK(morphableParameters().size() == 25);
    CHECK(morphDawParameterId(Slot::A, ProgramParameter::FilterCutoff) == "A_filterCutoffMorph");
    CHECK(morphDawParameterName(Slot::A, ProgramParameter::FilterCutoff) == "A: Filter Cutoff Morph");
    CHECK(morphDawParameterId(Slot::D, ProgramParameter::Lfo2Amount) == "D_lfo2AmountMorph");

    std::set<std::string> ids;
    for (auto slot : kAllSlots)
        for (auto parameter : morphableParameters()) {
            ids.insert(morphDawParameterId(slot, parameter));
            CHECK(morphAmountOf(parameter));
        }
    for (auto slot : kAllSlots)
        for (const auto& info : ccParameters())
            ids.insert(dawParameterId(slot, info.id));
    CHECK(ids.size() == 4 * (25 + kCcParameterCount));
}
