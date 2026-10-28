// The Library opens the official factory files [OFF-FB]. They may not be redistributed
// (ADR 0002), so this test is skipped unless they are present locally (NL2X_FACTORY_BANK_DIR).

#include "EditorAndSynth.h"

#include "nl2x/HardwareAssumptions.h"
#include "nl2x/Library.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

using namespace nl2x;
using nl2x::test::EditorAndSynth;
using DumpDestination = nl2x::test::SimulatedNordLead2X::DumpDestination;

namespace {

std::vector<std::uint8_t> readBytes(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
}

std::vector<LibraryFile> factoryLibrary()
{
    return scanLibrary(NL2X_FACTORY_BANK_DIR);
}

} // namespace

TEST_CASE("Factory Banks: the Library opens every factory file as a numbered Bank")
{
    const auto files = factoryLibrary();
    if (files.empty())
        SKIP("Factory Bank files not present in " << NL2X_FACTORY_BANK_DIR);

    int programBanks = 0;
    int performanceBanks = 0;
    for (const auto& file : files) {
        INFO(file.path.string());
        CHECK(file.ignoredMessageCount == 0);
        // Written back, every file is its own bytes.
        CHECK(toSysEx(file) == readBytes(file.path));

        if (file.kind == LibraryFileKind::ProgramBank) {
            ++programBanks;
            REQUIRE(file.entries.size() == 99 + 10);
            const auto bank = file.name.substr(4); // "bank0" … "bank3"
            CHECK(file.entries[0].name == "Bank " + bank + " \xE2\x80\x93 1");
            CHECK(file.entries[98].kind() == LibraryEntryKind::Program);
            CHECK(file.entries[99].name == "Bank " + bank + " \xE2\x80\x93 P0");
            CHECK(file.entries[108].kind() == LibraryEntryKind::PercussionKit);
        } else if (file.kind == LibraryFileKind::PerformanceBank) {
            ++performanceBanks;
            REQUIRE(file.entries.size() == 100);
            CHECK(file.entries[99].name == "Bank 0 \xE2\x80\x93 L9");
        } else {
            FAIL("unexpected kind " << libraryFileKindName(file.kind));
        }
    }
    CHECK(programBanks == 4);
    CHECK(performanceBanks == 1);
}

TEST_CASE("Factory Banks: every factory Program, Kit and Performance auditions without writing to a Bank")
{
    const auto files = factoryLibrary();
    if (files.empty())
        SKIP("Factory Bank files not present in " << NL2X_FACTORY_BANK_DIR);

    EditorAndSynth rig;
    rig.synth.playPercussionKit(Slot::D, {});
    rig.connect();

    int sent = 0;
    for (const auto& file : files)
        for (const auto& entry : file.entries) {
            const auto slot = entry.kind() == LibraryEntryKind::PercussionKit ? Slot::D : Slot::B;
            REQUIRE(audition(rig.engine, entry, slot, rig.now) == AuditionResult::Sent);
            rig.runFor(30);
            // A Performance makes the engine read the synth again.
            rig.runUntilSettled();
            ++sent;
        }

    CHECK(sent == 4 * 109 + 100);
    CHECK(rig.synth.receivedDumpCount(DumpDestination::Bank) == 0);
    CHECK(rig.synth.receivedDumpCount(DumpDestination::EditBuffer) == sent);
}
