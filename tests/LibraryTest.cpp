// The Library (#11): recognising .syx files, naming their entries, writing them back unchanged,
// saving Slots and Performances, and Audition. Fixtures are generated here with values we chose,
// into a folder under the build tree (never ~/Documents); Audition is checked against the
// simulated synth, looking only at the MIDI that crosses the boundary and at files written.

#include "EditorAndSynth.h"

#include "nl2x/HardwareAssumptions.h"
#include "nl2x/Library.h"
#include "nl2x/Performance.h"
#include "nl2x/Program.h"
#include "nl2x/SysEx.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace nl2x;
using nl2x::test::EditorAndSynth;
using nl2x::test::SimulatedNordLead2X;
using Catch::Matchers::ContainsSubstring;
using Bytes = std::vector<std::uint8_t>;
using DumpDestination = SimulatedNordLead2X::DumpDestination;

namespace {

constexpr GlobalChannel kDev = 15;

// Values that cover the whole signed byte, out-of-range ones included (−128 … 127).
std::vector<std::int8_t> values(std::size_t count, int seed)
{
    std::vector<std::int8_t> result(count);
    for (std::size_t i = 0; i < count; ++i)
        result[i] = static_cast<std::int8_t>(static_cast<std::uint8_t>(i * 13 + static_cast<std::size_t>(seed) * 37));
    return result;
}

Bytes programDump(std::uint8_t type, std::uint8_t spec, int seed)
{
    return buildSysEx(kDev, type, spec, values(kProgramParameterCount, seed));
}

Bytes kitDump(std::uint8_t type, std::uint8_t spec, int seed)
{
    return buildSysEx(kDev, type, spec, values(kPercussionKitParameterCount, seed));
}

Bytes performanceDump(std::uint8_t type, std::uint8_t spec, int seed)
{
    return buildSysEx(kDev, type, spec, values(kPerformanceParameterCount, seed));
}

void append(Bytes& bytes, const Bytes& more)
{
    bytes.insert(bytes.end(), more.begin(), more.end());
}

// A Program Bank laid out as the factory files are: 99 Program dumps then 10 Kit dumps, all with
// the same (unreliable) Bank type byte [OFF-FB].
Bytes programBank(std::uint8_t type, int seed)
{
    Bytes bytes;
    for (std::uint8_t location = 0; location < 99; ++location)
        append(bytes, programDump(type, location, seed + location));
    for (std::uint8_t kit = 0; kit < 10; ++kit)
        append(bytes, kitDump(type, static_cast<std::uint8_t>(hardware::kFirstPercussionKitLocation + kit), seed + kit));
    return bytes;
}

Bytes performanceBank(int seed)
{
    Bytes bytes;
    for (std::uint8_t location = 0; location < 100; ++location)
        append(bytes, performanceDump(hardware::kFirstPerformanceBankDumpType, location, seed + location));
    return bytes;
}

Bytes readBytes(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
}

void writeBytes(const std::filesystem::path& path, const Bytes& bytes)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

// A fresh, empty folder for one test, under the build tree.
std::filesystem::path freshFolder(const std::string& name)
{
    const auto folder = std::filesystem::path(NL2X_TEST_OUTPUT_DIR) / "library" / name;
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    return folder;
}

const LibraryFile& fileNamed(const std::vector<LibraryFile>& files, const std::string& name)
{
    const auto it = std::find_if(files.begin(), files.end(), [&name](const auto& file) { return file.name == name; });
    REQUIRE(it != files.end());
    return *it;
}

std::vector<Bytes> sysExSent(const EditorAndSynth& rig)
{
    std::vector<Bytes> sent;
    for (const auto& message : rig.toSynth.messages())
        if (! message.empty() && message.front() == kSysExStart)
            sent.push_back(message);
    return sent;
}

std::vector<Bytes> dumpsSent(const EditorAndSynth& rig)
{
    std::vector<Bytes> dumps;
    for (const auto& message : sysExSent(rig))
        if (message.size() > kSysExFramingSize)
            dumps.push_back(message);
    return dumps;
}

const std::string kDash = "\xE2\x80\x93";

} // namespace

TEST_CASE("The Library recognises every kind of .syx file and names its entries")
{
    const auto folder = freshFolder("recognition");
    writeBytes(folder / "Fat bass.syx", programDump(0, 1, 1));
    writeBytes(folder / "Drums.syx", kitDump(0, 17, 2));
    writeBytes(folder / "Live set.syx", performanceDump(30, 0, 3));
    writeBytes(folder / "bank3.syx", programBank(2, 4));
    writeBytes(folder / "Perf1.syx", performanceBank(5));
    Bytes mixed = programDump(1, 0, 6);
    append(mixed, performanceDump(31, 0, 7));
    writeBytes(folder / "Mixed.syx", mixed);
    writeBytes(folder / "Other synth.syx", Bytes { 0xF0, 0x43, 0x10, 0x01, 0x02, 0xF7 });
    writeBytes(folder / "Upper case.SYX", programDump(0, 0, 8));
    writeBytes(folder / "Sub folder" / "Pad.syx", programDump(0, 0, 9));
    writeBytes(folder / ".hidden.syx", programDump(0, 0, 10));
    writeBytes(folder / "Notes.txt", programDump(0, 0, 11));

    const auto files = scanLibrary(folder);

    CHECK(files.size() == 9);
    CHECK(fileNamed(files, "Fat bass").kind == LibraryFileKind::Program);
    CHECK(fileNamed(files, "Fat bass").entries.front().name == "Fat bass");
    CHECK(fileNamed(files, "Drums").kind == LibraryFileKind::PercussionKit);
    CHECK(fileNamed(files, "Drums").entries.front().kind() == LibraryEntryKind::PercussionKit);
    CHECK(fileNamed(files, "Live set").kind == LibraryFileKind::Performance);
    CHECK(fileNamed(files, "Mixed").kind == LibraryFileKind::Mixed);
    CHECK(fileNamed(files, "Upper case").kind == LibraryFileKind::Program);
    CHECK(fileNamed(files, "Pad").kind == LibraryFileKind::Program);
    CHECK(fileNamed(files, "Pad").path.parent_path().filename() == "Sub folder");

    const auto& other = fileNamed(files, "Other synth");
    CHECK(other.kind == LibraryFileKind::Unknown);
    CHECK(other.entries.empty());
    CHECK(other.ignoredMessageCount == 1);

    SECTION("a Program Bank shows its Programs numbered 1–99 and its Kits P0–P9")
    {
        const auto& bank = fileNamed(files, "bank3");
        CHECK(bank.kind == LibraryFileKind::ProgramBank);
        REQUIRE(bank.entries.size() == 109);
        CHECK(bank.entries[0].name == "Bank 3 " + kDash + " 1");
        CHECK(bank.entries[98].name == "Bank 3 " + kDash + " 99");
        CHECK(bank.entries[98].kind() == LibraryEntryKind::Program);
        CHECK(bank.entries[99].name == "Bank 3 " + kDash + " P0");
        CHECK(bank.entries[108].name == "Bank 3 " + kDash + " P9");
        CHECK(bank.entries[108].kind() == LibraryEntryKind::PercussionKit);
    }

    SECTION("a Performance Bank shows its Performances A0–L9")
    {
        const auto& bank = fileNamed(files, "Perf1");
        CHECK(bank.kind == LibraryFileKind::PerformanceBank);
        REQUIRE(bank.entries.size() == 100);
        CHECK(bank.entries[0].name == "Bank 1 " + kDash + " A0");
        CHECK(bank.entries[89].name == "Bank 1 " + kDash + " J9");
        CHECK(bank.entries[99].name == "Bank 1 " + kDash + " L9");
    }
}

TEST_CASE("Bank entries are named after the file, never after the type or spec bytes")
{
    // The factory files use type 2 for both bank0 and bank3 [OFF-FB].
    const auto withType2 = recogniseLibraryFile("bank0", programBank(2, 1));
    const auto withType9 = recogniseLibraryFile("bank0", programBank(9, 1));
    for (std::size_t i = 0; i < withType2.entries.size(); ++i)
        CHECK(withType2.entries[i].name == withType9.entries[i].name);

    const auto unnumbered = recogniseLibraryFile("My sounds", programBank(1, 1));
    CHECK(unnumbered.entries[16].name == "My sounds " + kDash + " 17");
}

TEST_CASE("Library files are written back byte for byte, unknown and out-of-range values included")
{
    for (const auto& bytes : { programDump(0, 2, 1), kitDump(0, 19, 2), performanceDump(30, 0, 3), programBank(3, 4),
                               performanceBank(5) }) {
        const auto file = recogniseLibraryFile("fixture", bytes);
        REQUIRE(file.ignoredMessageCount == 0);
        CHECK(toSysEx(file) == bytes);
    }

    SECTION("a single Edit Buffer dump opened and saved gives the same file")
    {
        const auto folder = freshFolder("round-trip");
        for (const auto& bytes : { programDump(0, 2, 1), kitDump(0, 18, 2), performanceDump(30, 0, 3) }) {
            writeBytes(folder / "in.syx", bytes);
            const auto file = openLibraryFile(folder / "in.syx");
            REQUIRE(file.entries.size() == 1);
            const auto out = uniqueLibraryPath(folder, "out");
            REQUIRE(saveLibraryFile(out, editBufferDumpOf(file.entries.front())) == LibrarySaveResult::Saved);
            CHECK(readBytes(out) == bytes);
            std::filesystem::remove(out);
        }
    }
}

TEST_CASE("An entry from a Bank file is saved as an Edit Buffer dump, never as a Bank dump")
{
    const auto bank = recogniseLibraryFile("bank1", programBank(3, 1));

    SECTION("a Program")
    {
        const auto saved = editBufferDumpOf(bank.entries[5]);
        const auto parsed = parseSysEx(saved);
        CHECK(parsed.kind == SysExKind::ProgramDump);
        CHECK(parsed.globalChannel == kDev);
        CHECK(parsed.type == hardware::kProgramEditBufferDumpType);
        CHECK(parsed.spec == 0);
        CHECK(parsed.values == parseSysEx(programDump(3, 5, 6)).values);
    }

    SECTION("a Percussion Kit, as its own file")
    {
        const auto folder = freshFolder("kit");
        const auto path = uniqueLibraryPath(folder, bank.entries[102].name);
        REQUIRE(saveLibraryFile(path, editBufferDumpOf(bank.entries[102])) == LibrarySaveResult::Saved);

        const auto file = openLibraryFile(path);
        CHECK(file.kind == LibraryFileKind::PercussionKit);
        const auto parsed = parseSysEx(readBytes(path));
        CHECK(parsed.type == hardware::kProgramEditBufferDumpType);
        CHECK(parsed.spec == hardware::kPercussionKitEditBufferSpecOfSlotA);
        CHECK(parsed.values == parseSysEx(kitDump(3, 102, 4)).values);
    }

    SECTION("a Performance")
    {
        const auto performances = recogniseLibraryFile("Perf0", performanceBank(1));
        const auto parsed = parseSysEx(editBufferDumpOf(performances.entries[42]));
        CHECK(parsed.type == hardware::kPerformanceEditBufferDumpType);
        CHECK(parsed.spec == 0);
        CHECK(parsed.values == parseSysEx(performanceDump(31, 42, 43)).values);
    }
}

TEST_CASE("Saving never replaces an existing file unless the user agreed")
{
    const auto folder = freshFolder("saving") / "Nord Lead 2X";
    const auto first = uniqueLibraryPath(folder, "Lead");
    CHECK(first == folder / "Lead.syx");

    // The folder is created when missing.
    REQUIRE(saveLibraryFile(first, programDump(0, 0, 1)) == LibrarySaveResult::Saved);
    CHECK(uniqueLibraryPath(folder, "Lead") == folder / "Lead 2.syx");

    CHECK(saveLibraryFile(first, programDump(0, 0, 2)) == LibrarySaveResult::AlreadyExists);
    CHECK(readBytes(first) == programDump(0, 0, 1));

    CHECK(saveLibraryFile(first, programDump(0, 0, 2), ReplaceExisting::Yes) == LibrarySaveResult::Saved);
    CHECK(readBytes(first) == programDump(0, 0, 2));

    CHECK(libraryFileName("Bank 0 " + kDash + " 12") == "Bank 0 " + kDash + " 12.syx");
    CHECK(libraryFileName("a/b:c") == "a-b-c.syx");
    CHECK(libraryFileName("  ") == "Untitled.syx");
    CHECK(libraryFileName("Done.SYX") == "Done.SYX");
}

TEST_CASE("Save Slot as .syx writes the Slot's Edit Buffer, with the edits made, as a standard Program dump")
{
    EditorAndSynth rig;
    rig.synth.setGlobalChannel(6);
    rig.connect();
    rig.engine.setParameter(Slot::B, CcParameter::FilterCutoff, 101);

    const auto file = slotProgramFile(rig.engine, Slot::B);
    REQUIRE(file);
    auto expected = rig.synth.editBuffer(Slot::B);
    CHECK(expected[3] == 101);
    CHECK(*file == programEditBufferDump(6, Slot::B, expected));

    const auto folder = freshFolder("save-slot");
    const auto path = uniqueLibraryPath(folder, "Slot B");
    REQUIRE(saveLibraryFile(path, *file) == LibrarySaveResult::Saved);
    const auto reopened = openLibraryFile(path);
    CHECK(reopened.kind == LibraryFileKind::Program);
    CHECK(reopened.name == "Slot B");
    CHECK(toSysEx(reopened) == *file);

    SECTION("but not a Slot playing a Percussion Kit")
    {
        rig.synth.playPercussionKit(Slot::C, {});
        rig.engine.readSlot(Slot::C, rig.now);
        rig.runUntilSettled();
        CHECK_FALSE(slotProgramFile(rig.engine, Slot::C));
    }
}

TEST_CASE("Save Performance as .syx writes a Performance Edit Buffer dump with the Slots' Programs")
{
    EditorAndSynth rig;
    CHECK_FALSE(performanceFile(rig.engine));
    rig.connect();
    rig.engine.setParameter(Slot::D, CcParameter::FilterCutoff, 9);

    const auto file = performanceFile(rig.engine);
    REQUIRE(file);
    const auto dump = parsePerformanceDump(*file);
    REQUIRE(dump);
    CHECK(dump->type == hardware::kPerformanceEditBufferDumpType);
    CHECK(dump->spec == 0);
    CHECK(dump->globalChannel == hardware::kFactoryGlobalChannel);
    CHECK(dump->performance.program(Slot::D) == *rig.engine.slotState(Slot::D).program);
    CHECK(dump->performance.program(Slot::D).value(ProgramParameter::FilterCutoff) == 9);
    CHECK(recogniseLibraryFile("Live", *file).kind == LibraryFileKind::Performance);
}

TEST_CASE("Audition sends a Program to the selected Slot's Edit Buffer and never writes to a Bank")
{
    EditorAndSynth rig;
    rig.connect();
    const auto banksBefore = rig.synth.bankProgram(0, 7);
    rig.toSynth.clear();

    const auto bank = recogniseLibraryFile("bank0", programBank(1, 20));
    const auto& entry = bank.entries[7];
    REQUIRE(audition(rig.engine, entry, Slot::C, rig.now) == AuditionResult::Sent);
    rig.runFor(200);

    const auto dumps = dumpsSent(rig);
    REQUIRE(dumps.size() == 1);
    const auto parsed = parseSysEx(dumps.front());
    CHECK(parsed.type == hardware::kProgramEditBufferDumpType);
    CHECK(parsed.spec == slotIndex(Slot::C));

    const auto& program = std::get<ProgramDump>(entry.dump).program;
    CHECK(std::equal(program.bytes().begin(), program.bytes().end(), rig.synth.editBuffer(Slot::C).begin()));
    CHECK(rig.engine.slotState(Slot::C).program == program);
    CHECK(rig.synth.receivedDumpCount(DumpDestination::Bank) == 0);
    CHECK(rig.synth.receivedDumpCount(DumpDestination::EditBuffer) == 1);
    CHECK(rig.synth.bankProgram(0, 7) == banksBefore);

    SECTION("a CC afterwards edits the auditioned Program")
    {
        rig.engine.setParameter(Slot::C, CcParameter::FilterCutoff, 33);
        CHECK(rig.synth.editBuffer(Slot::C)[3] == 33);
        CHECK(rig.engine.slotState(Slot::C).program->value(ProgramParameter::FilterCutoff) == 33);
    }
}

TEST_CASE("Auditioning every entry of a Bank file sends only Edit Buffer dumps")
{
    EditorAndSynth rig;
    rig.synth.playPercussionKit(Slot::D, {});
    rig.connect();
    rig.toSynth.clear();

    const auto bank = recogniseLibraryFile("bank2", programBank(4, 30));
    for (const auto& entry : bank.entries) {
        const auto slot = entry.kind() == LibraryEntryKind::PercussionKit ? Slot::D : Slot::A;
        CHECK(audition(rig.engine, entry, slot, rig.now) == AuditionResult::Sent);
        rig.runFor(30);
    }
    rig.runFor(500);

    for (const auto& dump : dumpsSent(rig))
        CHECK(parseSysEx(dump).type == hardware::kProgramEditBufferDumpType);
    CHECK(rig.synth.receivedDumpCount(DumpDestination::Bank) == 0);
    CHECK(rig.synth.receivedDumpCount(DumpDestination::EditBuffer) == 109);
    // The last Program and the last Kit are what the synth plays.
    const auto& lastProgram = std::get<ProgramDump>(bank.entries[98].dump).program;
    CHECK(std::equal(lastProgram.bytes().begin(), lastProgram.bytes().end(), rig.synth.editBuffer(Slot::A).begin()));
    REQUIRE(rig.synth.percussionKit(Slot::D));
    CHECK(rig.synth.percussionKit(Slot::D)->front() == std::get<PercussionKitDump>(bank.entries[108].dump).kit[0].bytes()[0]);
}

TEST_CASE("Rapid Auditions of one Slot send only the last one waiting")
{
    EditorAndSynth rig;
    rig.connect();
    rig.toSynth.clear();
    // A request waiting for its reply holds the dumps back.
    rig.engine.readSlot(Slot::A, rig.now);

    const auto bank = recogniseLibraryFile("bank0", programBank(1, 40));
    for (std::size_t i = 0; i < 5; ++i)
        REQUIRE(audition(rig.engine, bank.entries[i], Slot::B, rig.now) == AuditionResult::Sent);
    rig.runUntilSettled();
    rig.runFor(100);

    const auto dumps = dumpsSent(rig);
    REQUIRE(dumps.size() == 1);
    CHECK(parseSysEx(dumps.front()).values == parseSysEx(editBufferDumpOf(bank.entries[4])).values);
}

TEST_CASE("Audition matches Programs to Slots playing a Program and Kits to Slots playing a Kit")
{
    EditorAndSynth rig;
    SimulatedNordLead2X::PercussionKit playing {};
    playing.fill(3);
    rig.synth.playPercussionKit(Slot::B, playing);
    rig.connect();
    rig.toSynth.clear();

    const auto bank = recogniseLibraryFile("bank1", programBank(2, 50));
    const auto& program = bank.entries[0];
    const auto& kit = bank.entries[100];

    SECTION("a Program on a Slot playing a Kit is refused")
    {
        const auto result = audition(rig.engine, program, Slot::B, rig.now);
        CHECK(result == AuditionResult::RefusedSlotPlaysPercussionKit);
        CHECK_THAT(auditionRefusalText(result, Slot::B), ContainsSubstring("Slot B is playing a Percussion Kit"));
        rig.runFor(200);
        CHECK(sysExSent(rig).empty());
        CHECK(rig.engine.slotState(Slot::B).content == SlotContent::PercussionKit);
    }

    SECTION("a Kit on a Slot playing a Program is refused")
    {
        const auto result = audition(rig.engine, kit, Slot::A, rig.now);
        CHECK(result == AuditionResult::RefusedSlotPlaysProgram);
        CHECK_THAT(auditionRefusalText(result, Slot::A), ContainsSubstring("Slot A is playing a Program"));
        rig.runFor(200);
        CHECK(sysExSent(rig).empty());
    }

    SECTION("a Kit on a Slot playing a Kit goes to that Slot's Kit Edit Buffer")
    {
        REQUIRE(audition(rig.engine, kit, Slot::B, rig.now) == AuditionResult::Sent);
        rig.runFor(200);
        const auto dumps = dumpsSent(rig);
        REQUIRE(dumps.size() == 1);
        const auto parsed = parseSysEx(dumps.front());
        CHECK(parsed.kind == SysExKind::PercussionKitDump);
        CHECK(parsed.type == hardware::kProgramEditBufferDumpType);
        CHECK(parsed.spec == hardware::kPercussionKitEditBufferSpecOfSlotA + slotIndex(Slot::B));
        REQUIRE(rig.synth.percussionKit(Slot::B));
        CHECK(std::vector<std::int8_t>(rig.synth.percussionKit(Slot::B)->begin(), rig.synth.percussionKit(Slot::B)->end())
              == *parseSysEx(editBufferDumpOf(kit)).values);
        CHECK(rig.synth.receivedDumpCount(DumpDestination::Bank) == 0);
    }
}

TEST_CASE("Audition is refused before the synth has been found or a Slot read")
{
    EditorAndSynth rig;
    const auto program = recogniseLibraryFile("Lead", programDump(0, 0, 1));

    CHECK(audition(rig.engine, program.entries.front(), Slot::A, rig.now) == AuditionResult::RefusedNotConnected);
    rig.runFor(100);
    CHECK(rig.toSynth.messages().empty());

    SECTION("a Slot that was never read is not guessed at")
    {
        // The synth answers for Slot A, then stops answering.
        rig.engine.connect(std::nullopt, rig.now);
        for (int i = 0; i < 1000 && rig.engine.slotState(Slot::A).content == SlotContent::Unknown; ++i)
            rig.tick();
        rig.synth.setAnswersSysEx(false);
        rig.runUntilSettled();
        REQUIRE(rig.engine.slotState(Slot::D).content == SlotContent::Unknown);
        rig.toSynth.clear();

        const auto result = audition(rig.engine, program.entries.front(), Slot::D, rig.now);
        CHECK(result == AuditionResult::RefusedSlotNotRead);
        CHECK_THAT(auditionRefusalText(result, Slot::D), ContainsSubstring("Slot D has not been read"));
        rig.runFor(100);
        CHECK(dumpsSent(rig).empty());
    }
}

TEST_CASE("Auditioning a Performance sends it to the Performance Edit Buffer, then reads the synth again")
{
    EditorAndSynth rig;
    SimulatedNordLead2X::PercussionKit playing {};
    rig.synth.playPercussionKit(Slot::D, playing);
    rig.connect();
    rig.toSynth.clear();

    auto performanceValues = values(kPerformanceParameterCount, 60);
    // Slot Channels 1, 2, 3, 4 in the auditioned Performance.
    for (std::size_t slot = 0; slot < 4; ++slot)
        performanceValues[kPerformanceSlotChannelOffset + slot] = static_cast<std::int8_t>(slot);
    const auto bank = recogniseLibraryFile(
        "Perf2", [&] {
            Bytes bytes = performanceDump(31, 0, 1);
            append(bytes, buildSysEx(kDev, 32, 1, performanceValues));
            return bytes;
        }());
    REQUIRE(bank.kind == LibraryFileKind::PerformanceBank);

    REQUIRE(audition(rig.engine, bank.entries[1], Slot::A, rig.now) == AuditionResult::Sent);
    rig.runFor(50);
    rig.runUntilSettled();

    const auto dumps = dumpsSent(rig);
    REQUIRE(dumps.size() == 1);
    const auto parsed = parseSysEx(dumps.front());
    CHECK(parsed.type == hardware::kPerformanceEditBufferDumpType);
    CHECK(parsed.spec == 0);
    CHECK(rig.synth.receivedDumpCount(DumpDestination::Bank) == 0);

    // Read back: Slots A–C play the Performance's Programs, D keeps its Kit.
    const auto& performance = std::get<PerformanceDump>(bank.entries[1].dump).performance;
    for (auto slot : { Slot::A, Slot::B, Slot::C })
        CHECK(rig.engine.slotState(slot).program == performance.program(slot));
    CHECK(rig.engine.slotState(Slot::D).content == SlotContent::PercussionKit);
    CHECK(rig.engine.performanceStatus() == PerformanceStatus::Read);
    CHECK(rig.engine.slotChannel(Slot::C) == MidiChannel { 2 });
}
