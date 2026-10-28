// Project recall (#8): the project keeps the Synth model's sounds as a blob, and loading it sends
// them to the synth's Edit Buffers only, unless automatic sending is off. The engine is driven as
// the shell drives it, wired to the simulated synth; tests look at the MIDI crossing the boundary,
// the simulator's dump log and Edit Buffers, and the blob the engine hands the shell.

#include "EditorAndSynth.h"

#include "nl2x/HardwareAssumptions.h"
#include "nl2x/ProjectState.h"
#include "nl2x/SysEx.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

using namespace nl2x;
using nl2x::test::EditorAndSynth;
using nl2x::test::SimulatedNordLead2X;
using DumpDestination = SimulatedNordLead2X::DumpDestination;

namespace {

using Bytes = std::vector<std::uint8_t>;

constexpr std::size_t kSplitPointSetting = 353 - kPerformanceSlotChannelOffset;
// Long enough for a Performance dump and four Program dumps, and any reads after them.
constexpr TimeMs kRecallTimeMs = 3000;

// A project made on one synth: the Slots read from it, edited by CC and by Morph Amount, and the
// Performance with an edited Split Point. Slot C plays a Kit when `kitOnC`.
Bytes savedProject(bool kitOnC = false)
{
    EditorAndSynth maker;
    for (auto slot : kAllSlots)
        maker.synth.setEditBuffer(slot, EditorAndSynth::someProgram(40 + static_cast<int>(slotIndex(slot))));
    if (kitOnC)
        maker.synth.playPercussionKit(Slot::C, {});
    maker.connect();
    maker.engine.setParameter(Slot::A, CcParameter::FilterCutoff, 99);
    REQUIRE(maker.engine.setMorphAmount(Slot::B, ProgramParameter::FilterCutoff, -77, maker.now)
            == MorphEditResult::Accepted);
    REQUIRE(maker.engine.setPerformanceSetting(PerformanceSetting::SplitPoint, Slot::A, 72)
            == PerformanceEditResult::Accepted);
    maker.runFor(500);
    return maker.engine.projectState();
}

std::size_t messagesSent(const EditorAndSynth& rig)
{
    return rig.toSynth.messages().size();
}

// Messages sent to the synth from index `from` on.
std::vector<Bytes> sentSince(const EditorAndSynth& rig, std::size_t from)
{
    const auto& all = rig.toSynth.messages();
    return { all.begin() + static_cast<std::ptrdiff_t>(from), all.end() };
}

bool isControlChange(const Bytes& message)
{
    return message.size() == 3 && (message[0] & 0xF0) == 0xB0;
}

bool isSlotRequest(const Bytes& message, Slot slot)
{
    const auto parsed = parseSysEx(message);
    return parsed.kind == SysExKind::Other && message.size() == kSysExFramingSize
           && parsed.type == hardware::kSlotEditBufferRequestType && parsed.spec == slotIndex(slot);
}

bool isPerformanceRequest(const Bytes& message)
{
    const auto parsed = parseSysEx(message);
    return parsed.kind == SysExKind::Other && message.size() == kSysExFramingSize
           && parsed.type == hardware::kPerformanceEditBufferRequestType;
}

std::size_t countIf(const std::vector<Bytes>& messages, auto predicate)
{
    return static_cast<std::size_t>(std::count_if(messages.begin(), messages.end(), predicate));
}

// The synth's Edit Buffers equal the project's: every Slot playing a Program, and the Performance
// settings the synth receives (Split Point stands for them).
void requireSynthHoldsProject(const EditorAndSynth& rig, const Bytes& project)
{
    const auto sounds = decodeProjectState(project);
    REQUIRE(sounds);
    for (auto slot : kAllSlots) {
        const auto& saved = sounds->slots[slotIndex(slot)];
        if (saved.content == SlotContent::Program)
            CHECK(rig.synth.editBuffer(slot) == saved.program->bytes());
    }
    REQUIRE(sounds->performance);
    CHECK(rig.synth.performanceSettings()[kSplitPointSetting] == 72);
}

} // namespace

TEST_CASE("The project state round-trips byte for byte, unknown values included")
{
    ProjectSounds sounds;
    Program::Values extreme;
    for (std::size_t i = 0; i < extreme.size(); ++i)
        extreme[i] = static_cast<std::int8_t>(i % 2 == 0 ? -128 : 127);
    sounds.slots[0] = { SlotContent::Program, Program(extreme) };
    sounds.slots[1] = { SlotContent::PercussionKit, std::nullopt };
    sounds.slots[3] = { SlotContent::Program, Program(EditorAndSynth::someProgram(9)) };
    Performance::Values performance;
    for (std::size_t i = 0; i < performance.size(); ++i)
        performance[i] = static_cast<std::int8_t>(static_cast<std::uint8_t>(i * 13));
    sounds.performance = Performance(performance);

    const auto blob = encodeProjectState(sounds);
    const auto decoded = decodeProjectState(blob);
    REQUIRE(decoded);
    CHECK(*decoded == sounds);
    CHECK(encodeProjectState(*decoded) == blob);

    SECTION("through an engine that is not connected")
    {
        EditorAndSynth rig;
        CHECK(rig.engine.loadProjectState(blob, false, rig.now) == ProjectLoadResult::NotSent);
        CHECK(rig.engine.projectState() == blob);
    }
}

TEST_CASE("A project saved from a connected engine round-trips through a fresh one")
{
    const auto project = savedProject();
    EditorAndSynth rig;
    rig.engine.loadProjectState(project, true, rig.now);
    CHECK(rig.engine.projectState() == project);
}

TEST_CASE("Loading a project while connected sends it to the Edit Buffers only")
{
    const auto project = savedProject();
    EditorAndSynth rig;
    rig.connect();
    const auto sentBefore = messagesSent(rig);
    const auto dumpsBefore = rig.synth.receivedDumps().size();

    CHECK(rig.engine.loadProjectState(project, true, rig.now) == ProjectLoadResult::Sending);
    rig.runFor(kRecallTimeMs);

    const std::vector<SimulatedNordLead2X::ReceivedDump> dumps(
        rig.synth.receivedDumps().begin() + static_cast<std::ptrdiff_t>(dumpsBefore), rig.synth.receivedDumps().end());
    CHECK(rig.synth.receivedDumpCount(DumpDestination::Bank) == 0);
    // The Performance first, then one Program dump per Slot.
    REQUIRE(dumps.size() == 5);
    CHECK(dumps[0].kind == SysExKind::PerformanceDump);
    CHECK(dumps[0].type == hardware::kPerformanceEditBufferDumpType);
    for (std::size_t i = 1; i < dumps.size(); ++i) {
        CHECK(dumps[i].kind == SysExKind::ProgramDump);
        CHECK(dumps[i].destination == DumpDestination::EditBuffer);
        CHECK(dumps[i].spec == i - 1);
    }
    requireSynthHoldsProject(rig, project);

    const auto sent = sentSince(rig, sentBefore);
    // No CC (the DAW parameters' restored values never reach the synth), and nothing read back
    // over the project's sounds.
    CHECK(countIf(sent, isControlChange) == 0);
    CHECK(countIf(sent, isPerformanceRequest) == 0);
    for (auto slot : kAllSlots)
        CHECK(countIf(sent, [slot](const Bytes& m) { return isSlotRequest(m, slot); }) == 0);
    // The model is the project's state.
    CHECK(rig.engine.projectState() == project);
    CHECK(rig.engine.performanceStatus() == PerformanceStatus::Read);
}

TEST_CASE("Project dumps are paced like every other dump")
{
    const auto project = savedProject();
    EditorAndSynth rig;
    rig.connect();
    const auto timesBefore = rig.sendTimes.size();
    rig.engine.loadProjectState(project, true, rig.now);
    rig.runFor(kRecallTimeMs);
    const std::vector<TimeMs> times(rig.sendTimes.begin() + static_cast<std::ptrdiff_t>(timesBefore),
                                    rig.sendTimes.end());
    REQUIRE(times.size() == 5);
    for (std::size_t i = 1; i < times.size(); ++i)
        CHECK(times[i] - times[i - 1] >= hardware::kRequestSpacingMs);
}

TEST_CASE("A project loaded before connecting is sent instead of reading the Slots")
{
    const auto project = savedProject();
    EditorAndSynth rig;
    rig.synth.setGlobalChannel(6);

    CHECK(rig.engine.loadProjectState(project, true, rig.now) == ProjectLoadResult::HeldUntilConnected);
    CHECK(messagesSent(rig) == 0);
    // Shown at once, before the synth is found.
    CHECK(rig.engine.projectState() == project);

    rig.connect();
    rig.runFor(kRecallTimeMs);

    CHECK(rig.engine.globalChannel() == GlobalChannel { 6 });
    CHECK(rig.engine.connectionStatus() == ConnectionStatus::Connected);
    CHECK(rig.synth.receivedDumpCount(DumpDestination::Bank) == 0);
    CHECK(rig.synth.receivedDumpCount(DumpDestination::EditBuffer) == 5);
    requireSynthHoldsProject(rig, project);

    const auto& sent = rig.toSynth.messages();
    // Only the probe asked for a Slot (A, on each candidate channel); B–D and the Performance
    // were never read, and Slot A's answer did not replace the project's Slot A.
    for (auto slot : { Slot::B, Slot::C, Slot::D })
        CHECK(countIf(sent, [slot](const Bytes& m) { return isSlotRequest(m, slot); }) == 0);
    CHECK(countIf(sent, isPerformanceRequest) == 0);
    CHECK(countIf(sent, isControlChange) == 0);
    CHECK(rig.engine.projectState() == project);
}

TEST_CASE("A project whose recall was cut short by a new connection is sent again")
{
    const auto project = savedProject();
    EditorAndSynth rig;
    rig.connect();
    rig.engine.loadProjectState(project, true, rig.now);
    // The interface is replugged after the first dump went out.
    rig.engine.connect(std::nullopt, rig.now);
    rig.runUntilSettled();
    rig.runFor(kRecallTimeMs);

    CHECK(rig.synth.receivedDumpCount(DumpDestination::Bank) == 0);
    requireSynthHoldsProject(rig, project);
    CHECK(rig.engine.projectState() == project);
}

TEST_CASE("Kit Slots are not sent and are read once the project's sounds are out")
{
    const auto project = savedProject(true);
    REQUIRE(decodeProjectState(project)->slots[slotIndex(Slot::C)].content == SlotContent::PercussionKit);

    EditorAndSynth rig;
    rig.synth.playPercussionKit(Slot::C, {});
    rig.connect();
    const auto sentBefore = messagesSent(rig);
    const auto dumpsBefore = rig.synth.receivedDumps().size();
    rig.engine.loadProjectState(project, true, rig.now);
    rig.runFor(kRecallTimeMs);

    const auto& log = rig.synth.receivedDumps();
    std::vector<std::uint8_t> programSpecs;
    for (auto it = log.begin() + static_cast<std::ptrdiff_t>(dumpsBefore); it != log.end(); ++it) {
        CHECK(it->destination == DumpDestination::EditBuffer);
        CHECK(it->kind != SysExKind::PercussionKitDump);
        if (it->kind == SysExKind::ProgramDump)
            programSpecs.push_back(it->spec);
    }
    CHECK(programSpecs == std::vector<std::uint8_t> { 0, 1, 3 });

    const auto sent = sentSince(rig, sentBefore);
    CHECK(countIf(sent, [](const Bytes& m) { return isSlotRequest(m, Slot::C); }) == 1);
    for (auto slot : { Slot::A, Slot::B, Slot::D })
        CHECK(countIf(sent, [slot](const Bytes& m) { return isSlotRequest(m, slot); }) == 0);
    CHECK(rig.engine.slotState(Slot::C).content == SlotContent::PercussionKit);
}

TEST_CASE("With automatic sending off, loading a project sends nothing and the synth is read")
{
    const auto project = savedProject();

    SECTION("loaded while connected")
    {
        EditorAndSynth rig;
        rig.connect();
        const auto sentBefore = messagesSent(rig);
        const auto dumpsBefore = rig.synth.receivedDumps().size();
        CHECK(rig.engine.loadProjectState(project, false, rig.now) == ProjectLoadResult::NotSent);
        rig.runUntilSettled();
        rig.runFor(500);

        CHECK(rig.synth.receivedDumps().size() == dumpsBefore);
        const auto sent = sentSince(rig, sentBefore);
        CHECK(countIf(sent, isControlChange) == 0);
        CHECK(countIf(sent, isPerformanceRequest) == 1);
        for (auto slot : kAllSlots) {
            CHECK(countIf(sent, [slot](const Bytes& m) { return isSlotRequest(m, slot); }) == 1);
            // The synth's sounds replace the project's.
            CHECK(rig.engine.slotState(slot).program->bytes() == rig.synth.editBuffer(slot));
        }
    }

    SECTION("loaded before connecting")
    {
        EditorAndSynth rig;
        CHECK(rig.engine.loadProjectState(project, false, rig.now) == ProjectLoadResult::NotSent);
        rig.connect();
        rig.runFor(500);

        CHECK(rig.synth.receivedDumps().empty());
        const auto& sent = rig.toSynth.messages();
        CHECK(countIf(sent, isControlChange) == 0);
        for (auto slot : kAllSlots) {
            CHECK(countIf(sent, [slot](const Bytes& m) { return isSlotRequest(m, slot); }) >= 1);
            CHECK(rig.engine.slotState(slot).program->bytes() == rig.synth.editBuffer(slot));
        }
        CHECK(rig.engine.performanceStatus() == PerformanceStatus::Read);
    }
}

TEST_CASE("A blob that is not a project state of this version is rejected and nothing is sent")
{
    const auto project = savedProject();
    auto newer = project;
    newer[4] = static_cast<std::uint8_t>(kProjectStateFormatVersion + 1);
    auto older = project;
    older[4] = 0;
    auto foreign = project;
    foreign[0] = 'X';
    auto badContent = project;
    badContent[5] = 7;
    const Bytes truncated(project.begin(), project.end() - 1);
    auto padded = project;
    padded.push_back(0);

    for (const auto& blob : { newer, older, foreign, badContent, truncated, padded, Bytes {} }) {
        CHECK_FALSE(decodeProjectState(blob));

        EditorAndSynth rig;
        rig.connect();
        const auto before = rig.engine.projectState();
        const auto sentBefore = messagesSent(rig);
        CHECK(rig.engine.loadProjectState(blob, true, rig.now) == ProjectLoadResult::Rejected);
        rig.runFor(kRecallTimeMs);
        CHECK(messagesSent(rig) == sentBefore);
        CHECK(rig.engine.projectState() == before);
    }

    SECTION("before connecting, the synth is then read as usual")
    {
        EditorAndSynth rig;
        CHECK(rig.engine.loadProjectState(truncated, true, rig.now) == ProjectLoadResult::Rejected);
        rig.connect();
        rig.runFor(500);
        CHECK(rig.synth.receivedDumps().empty());
        for (auto slot : kAllSlots)
            CHECK(rig.engine.slotState(slot).program->bytes() == rig.synth.editBuffer(slot));
    }
}

TEST_CASE("Loading a project never sends a CC, so restored DAW parameters reach the synth only as dumps")
{
    // The shell's part (described in PluginProcessor.cpp): restored DAW parameter values are taken
    // as already in sync and then follow the engine; only the engine's dumps carry the sounds.
    const auto project = savedProject();
    for (bool sendToSynth : { true, false }) {
        EditorAndSynth rig;
        rig.connect();
        const auto sentBefore = messagesSent(rig);
        rig.engine.loadProjectState(project, sendToSynth, rig.now);
        rig.runFor(kRecallTimeMs);
        CHECK(countIf(sentSince(rig, sentBefore), isControlChange) == 0);
    }
}
