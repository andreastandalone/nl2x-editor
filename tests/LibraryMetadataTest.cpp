// The user's information about Library sounds (#39): custom names, categories and favourites kept
// in library.json, found by a key made from the parameter values, filtered, and never lost when
// the file cannot be read. Fixtures are generated here into a folder under the build tree; the
// tests look only at what the Library shows and at the files written.

#include "nl2x/HardwareAssumptions.h"
#include "nl2x/Library.h"
#include "nl2x/LibraryMetadata.h"
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
using Catch::Matchers::ContainsSubstring;
using Bytes = std::vector<std::uint8_t>;

namespace {

constexpr GlobalChannel kDev = 15;

std::vector<std::int8_t> values(std::size_t count, int seed)
{
    std::vector<std::int8_t> result(count);
    for (std::size_t i = 0; i < count; ++i)
        result[i] = static_cast<std::int8_t>(static_cast<std::uint8_t>(i * 7 + static_cast<std::size_t>(seed) * 41));
    return result;
}

Bytes programDump(std::uint8_t type, std::uint8_t spec, int seed)
{
    return buildSysEx(kDev, type, spec, values(kProgramParameterCount, seed));
}

Bytes performanceDump(int seed)
{
    return buildSysEx(kDev, hardware::kPerformanceEditBufferDumpType, 0, values(kPerformanceParameterCount, seed));
}

Bytes programBank(int firstSeed, int count)
{
    Bytes bytes;
    for (int location = 0; location < count; ++location) {
        const auto dump = programDump(5, static_cast<std::uint8_t>(location), firstSeed + location);
        bytes.insert(bytes.end(), dump.begin(), dump.end());
    }
    return bytes;
}

void writeBytes(const std::filesystem::path& path, const Bytes& bytes)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void writeText(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << text;
}

std::string readText(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
}

std::filesystem::path freshFolder(const std::string& name)
{
    const auto folder = std::filesystem::path(NL2X_TEST_OUTPUT_DIR) / "library-metadata" / name;
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    return folder;
}

LibraryEntry onlyEntry(const Bytes& bytes, const std::string& name = "Sound")
{
    auto file = recogniseLibraryFile(name, bytes);
    REQUIRE(file.entries.size() == 1);
    return file.entries.front();
}

LibraryEntry programEntry(const std::vector<std::int8_t>& programValues, const std::string& name = "Sound")
{
    return onlyEntry(buildSysEx(kDev, hardware::kProgramEditBufferDumpType, 0, programValues), name);
}

// `programValues` with the first `count` values changed, as turning `count` knobs would.
std::vector<std::int8_t> edited(std::vector<std::int8_t> programValues, std::size_t count, int by = 1)
{
    for (std::size_t i = 0; i < count; ++i)
        programValues[i] = static_cast<std::int8_t>(programValues[i] + by);
    return programValues;
}

std::vector<std::string> names(const std::vector<LibraryEntryRef>& refs, const LibraryMetadata& metadata)
{
    std::vector<std::string> result;
    for (const auto& ref : refs)
        result.push_back(metadata.displayName(*ref.entry));
    return result;
}

} // namespace

TEST_CASE("A sound's fingerprint is its parameter values, without header bytes")
{
    const auto program = onlyEntry(programDump(hardware::kProgramEditBufferDumpType, 0, 0));
    const auto bank = recogniseLibraryFile("bank2", programBank(0, 3));
    CHECK(SoundFingerprint::of(bank.entries.front()) == SoundFingerprint::of(program));
    CHECK(SoundFingerprint::of(onlyEntry(programDump(hardware::kProgramEditBufferDumpType, 2, 0)))
          == SoundFingerprint::of(program));
    CHECK(soundDifferences(SoundFingerprint::of(bank.entries[1]), SoundFingerprint::of(program)) > 0);
    CHECK(SoundFingerprint::of(program).values.size() == kProgramParameterCount);
    CHECK(SoundFingerprint::of(onlyEntry(performanceDump(0))).values.size() == kPerformanceParameterCount);
    CHECK(soundDifferences(SoundFingerprint::of(program), SoundFingerprint::of(onlyEntry(performanceDump(0)))) == -1);
}

TEST_CASE("Tags belong to the sound: exact match, edited sound, different sound, sound restored")
{
    const auto folder = freshFolder("identity");
    const auto original = values(kProgramParameterCount, 3);
    auto metadata = LibraryMetadata::load(folder);
    REQUIRE(metadata.set(programEntry(original), { "Fat bass", "Bass", true }) == MetadataSaveResult::Saved);
    const SoundInfo tags { "Fat bass", "Bass", true };

    SECTION("the same values anywhere are the same sound")
    {
        CHECK(metadata.info(programEntry(original, "elsewhere")) == tags);
        CHECK(metadata.match(SoundFingerprint::of(programEntry(original)))->differences == 0);
    }

    SECTION("a sound edited on the synth panel keeps its tags")
    {
        for (std::size_t knobs = 1; knobs <= static_cast<std::size_t>(kSimilarSoundMaxDifferences); ++knobs)
            CHECK(metadata.info(programEntry(edited(original, knobs))) == tags);
    }

    SECTION("a clearly different sound gets none")
    {
        CHECK(metadata.info(programEntry(edited(original, kSimilarSoundMaxDifferences + 1))).isEmpty());
        CHECK(metadata.info(programEntry(values(kProgramParameterCount, 4))).isEmpty());
    }

    SECTION("an edited copy is a variation while the sound itself is still in the Library")
    {
        const auto variation = programEntry(edited(original, 2));
        LibraryFile file;
        file.entries.push_back(programEntry(original));
        metadata.setLibraryContents({ file });
        CHECK(metadata.info(variation).isEmpty());
        CHECK(metadata.info(programEntry(original)) == tags);

        // Backups do not count: they hold old versions on purpose.
        file.isBackup = true;
        metadata.setLibraryContents({ file });
        CHECK(metadata.info(variation) == tags);
    }

    SECTION("a location shows whichever sound is there: another sound, then the first one again")
    {
        const auto location = folder / "Slot A.syx";
        const auto shown = [&] {
            const auto files = scanLibrary(folder);
            auto view = LibraryMetadata::load(folder);
            view.setLibraryContents(files);
            return view.info(files.front().entries.front());
        };
        writeBytes(location, buildSysEx(kDev, 0, 0, original));
        CHECK(shown() == tags);
        writeBytes(location, buildSysEx(kDev, 0, 0, values(kProgramParameterCount, 9)));
        CHECK(shown().isEmpty());
        writeBytes(location, buildSysEx(kDev, 0, 0, original));
        CHECK(shown() == tags);
    }

    SECTION("two tagged sounds equally close: neither is guessed")
    {
        auto other = original;
        other[10] = static_cast<std::int8_t>(other[10] + 5);
        other[11] = static_cast<std::int8_t>(other[11] + 5);
        // Tagged as a sound of its own while the first one is in the Library, then both files leave it.
        LibraryFile file;
        file.entries.push_back(programEntry(original));
        metadata.setLibraryContents({ file });
        REQUIRE(metadata.set(programEntry(other), { "Other", {}, false }) == MetadataSaveResult::Saved);
        metadata.setLibraryContents({});
        auto between = original;
        between[10] = other[10];
        CHECK(metadata.info(programEntry(between)).isEmpty());
        CHECK(metadata.info(programEntry(edited(original, 1))) == tags);
    }

    SECTION("tagging an edited sound updates the sound, and its latest values become its fingerprint")
    {
        const auto newer = edited(original, 3);
        REQUIRE(metadata.set(programEntry(newer), { "Fatter bass", "Bass", true }) == MetadataSaveResult::Saved);
        const auto reloaded = LibraryMetadata::load(folder);
        CHECK(reloaded.match(SoundFingerprint::of(programEntry(newer)))->differences == 0);
        CHECK(reloaded.info(programEntry(original)).name == "Fatter bass");
        CHECK(reloaded.info(programEntry(edited(newer, 4, 2))).name == "Fatter bass");
        CHECK(readText(folder / "library.json").find("\"id\": \"s2\"") == std::string::npos);
    }
}

TEST_CASE("Edits made in the editor keep a sound's tags however far they go")
{
    const auto folder = freshFolder("lineage");
    const auto original = values(kProgramParameterCount, 3);
    auto metadata = LibraryMetadata::load(folder);
    REQUIRE(metadata.set(programEntry(original), { "Pluck", "Keys", false }) == MetadataSaveResult::Saved);
    const auto id = metadata.match(SoundFingerprint::of(programEntry(original)))->soundId;

    SoundLineage lineage;
    lineage.loaded("Slot A", id, SoundFingerprint::of(programEntry(original)));
    auto current = original;
    for (std::size_t knob = 0; knob < 20; ++knob) {
        current[knob] = static_cast<std::int8_t>(current[knob] + 3);
        lineage.observe("Slot A", SoundFingerprint::of(programEntry(current)));
    }
    REQUIRE(lineage.soundAt("Slot A") == id);
    CHECK(metadata.info(programEntry(current)).isEmpty());

    // "Save Slot as .syx": the sound now has these values.
    REQUIRE(metadata.followEdit(id, SoundFingerprint::of(programEntry(current))) == MetadataSaveResult::Saved);
    CHECK(LibraryMetadata::load(folder).info(programEntry(current)).name == "Pluck");

    SECTION("another Program selected on the synth breaks the link")
    {
        lineage.observe("Slot A", SoundFingerprint::of(programEntry(values(kProgramParameterCount, 7))));
        CHECK_FALSE(lineage.soundAt("Slot A"));
    }

    SECTION("a Slot that no longer plays a Program breaks it too")
    {
        lineage.observe("Slot A", std::nullopt);
        CHECK_FALSE(lineage.soundAt("Slot A"));
        CHECK_FALSE(lineage.soundAt("Slot B"));
    }

    SECTION("values another sound already has are left to that sound")
    {
        const auto taken = values(kProgramParameterCount, 8);
        REQUIRE(metadata.set(programEntry(taken), { "Taken", {}, false }) == MetadataSaveResult::Saved);
        REQUIRE(metadata.followEdit(id, SoundFingerprint::of(programEntry(taken))) == MetadataSaveResult::Saved);
        CHECK(LibraryMetadata::load(folder).info(programEntry(taken)).name == "Taken");
        CHECK(LibraryMetadata::load(folder).info(programEntry(current)).name == "Pluck");
    }
}

TEST_CASE("Information set on a sound is saved to library.json and found again after renaming and moving")
{
    const auto folder = freshFolder("round-trip");
    writeBytes(folder / "Fat.syx", programDump(hardware::kProgramEditBufferDumpType, 0, 1));
    writeBytes(folder / "bank1.syx", programBank(10, 5));

    auto metadata = LibraryMetadata::load(folder);
    REQUIRE(metadata.status() == LibraryMetadataStatus::Ready);
    CHECK_FALSE(std::filesystem::exists(metadata.filePath()));

    auto files = scanLibrary(folder);
    const auto& single = files[0].name == "Fat" ? files[0] : files[1];
    const auto& bank = files[0].name == "Fat" ? files[1] : files[0];
    CHECK(metadata.displayName(single.entries.front()) == "Fat");
    CHECK(metadata.displayName(bank.entries[2]) == "Bank 1 \xE2\x80\x93 3");

    const SoundInfo fat { "Fat \"Moog\" bass \xC3\xA8", "Bass", true };
    REQUIRE(metadata.set(single.entries.front(), fat) == MetadataSaveResult::Saved);
    REQUIRE(metadata.set(bank.entries[2], { {}, "Pad", false }) == MetadataSaveResult::Saved);
    CHECK(metadata.displayName(single.entries.front()) == fat.name);
    CHECK(metadata.displayName(bank.entries[2]) == "Bank 1 \xE2\x80\x93 3");

    const auto text = readText(folder / "library.json");
    CHECK_THAT(text, ContainsSubstring("\"version\": 1"));
    CHECK_THAT(text, ContainsSubstring("\"name\": \"Fat \\\"Moog\\\" bass \xC3\xA8\""));

    // The user renames the file and moves it into a subfolder.
    std::filesystem::create_directories(folder / "Basses");
    std::filesystem::rename(folder / "Fat.syx", folder / "Basses" / "renamed.syx");
    const auto reloaded = LibraryMetadata::load(folder);
    const auto moved = scanLibrary(folder);
    const auto it = std::find_if(moved.begin(), moved.end(), [](const auto& f) { return f.name == "renamed"; });
    REQUIRE(it != moved.end());
    CHECK(reloaded.info(it->entries.front()) == fat);

    SECTION("an empty SoundInfo removes the sound from the file")
    {
        auto again = reloaded;
        REQUIRE(again.set(it->entries.front(), {}) == MetadataSaveResult::Saved);
        CHECK(LibraryMetadata::load(folder).info(it->entries.front()).isEmpty());
        CHECK(LibraryMetadata::load(folder).info(bank.entries[2]).category == "Pad");
    }
}

TEST_CASE("Every save reads the file again, so two windows keep each other's changes")
{
    const auto folder = freshFolder("two-windows");
    const auto a = onlyEntry(programDump(hardware::kProgramEditBufferDumpType, 0, 1));
    const auto b = onlyEntry(programDump(hardware::kProgramEditBufferDumpType, 0, 2));

    auto first = LibraryMetadata::load(folder);
    auto second = LibraryMetadata::load(folder);
    REQUIRE(first.set(a, { "A", {}, false }) == MetadataSaveResult::Saved);
    REQUIRE(second.set(b, { "B", {}, false }) == MetadataSaveResult::Saved);

    const auto both = LibraryMetadata::load(folder);
    CHECK(both.displayName(a) == "A");
    CHECK(both.displayName(b) == "B");
    CHECK(second.displayName(a) == "A");
}

TEST_CASE("A file that cannot be read is never overwritten")
{
    const auto folder = freshFolder("unreadable");
    const auto entry = onlyEntry(programDump(hardware::kProgramEditBufferDumpType, 0, 1));

    const std::string damaged = "{ \"format\": \"NL2X Editor Library\", \"version\": 1, \"sounds\": [ { \"id\": ";
    const std::string newer = R"({ "format": "NL2X Editor Library", "version": 2, "sounds": [] })";
    const std::string wrongType = R"({ "format": "NL2X Editor Library", "version": 1, "sounds": [ { "id": "s1", "kind": "program", "values": "00", "favourite": true } ] })";
    const std::string notOurs = R"({ "something": "else" })";
    for (const auto& text : { damaged, newer, wrongType, notOurs }) {
        writeText(folder / "library.json", text);
        auto metadata = LibraryMetadata::load(folder);
        CHECK(metadata.status() == LibraryMetadataStatus::Unreadable);
        CHECK(metadata.set(entry, { "Lost?", {}, false }) == MetadataSaveResult::RefusedUnreadable);
        CHECK(readText(folder / "library.json") == text);
        CHECK(metadata.displayName(entry) == "Sound");
    }

    SECTION("once the user fixes the file, saving works again")
    {
        auto metadata = LibraryMetadata::load(folder);
        std::filesystem::remove(folder / "library.json");
        CHECK(metadata.set(entry, { "Fixed", {}, false }) == MetadataSaveResult::Saved);
        CHECK(metadata.status() == LibraryMetadataStatus::Ready);
    }
}

TEST_CASE("Saving replaces the file in one step and keeps the previous version as library.json.bak")
{
    const auto folder = freshFolder("atomic");
    const auto entry = onlyEntry(programDump(hardware::kProgramEditBufferDumpType, 0, 1));
    auto metadata = LibraryMetadata::load(folder);
    REQUIRE(metadata.set(entry, { "One", {}, false }) == MetadataSaveResult::Saved);
    const auto firstVersion = readText(folder / "library.json");
    REQUIRE(metadata.set(entry, { "Two", {}, false }) == MetadataSaveResult::Saved);

    CHECK(readText(folder / "library.json.bak") == firstVersion);
    CHECK_THAT(readText(folder / "library.json"), ContainsSubstring("\"Two\""));
    CHECK_FALSE(std::filesystem::exists(folder / "library.json.tmp"));
}

TEST_CASE("A file written or edited by hand is read: escapes, extra fields, other categories")
{
    const auto folder = freshFolder("by-hand");
    const auto entry = onlyEntry(programDump(hardware::kProgramEditBufferDumpType, 0, 0));
    std::string hex;
    for (const auto value : values(kProgramParameterCount, 0)) {
        hex += "0123456789ABCDEF"[static_cast<std::uint8_t>(value) >> 4];
        hex += "0123456789ABCDEF"[static_cast<std::uint8_t>(value) & 0xF];
    }
    writeText(folder / "library.json", R"({
  "format": "NL2X Editor Library", "version": 1, "comment": [1, 2.5e3, null, {"x": false}],
  "sounds": [
    { "id": "s7", "name": "Caf\u00e9 \ud83c\udfb9\ttab", "category": "Choir", "favourite": true, "rating": 5,
      "kind": "program", "values": ")" + hex + R"(" }
  ]
})");
    const auto metadata = LibraryMetadata::load(folder);
    REQUIRE(metadata.status() == LibraryMetadataStatus::Ready);
    CHECK(metadata.info(entry) == SoundInfo { "Caf\xC3\xA9 \xF0\x9F\x8E\xB9\ttab", "Choir", true });

    const auto choices = metadata.categoryChoices();
    REQUIRE(choices.size() == kLibraryCategories.size() + 1);
    CHECK(choices.front() == "Bass");
    CHECK(choices.back() == "Choir");
}

TEST_CASE("The Library is filtered by category and favourites, backups left out")
{
    const auto folder = freshFolder("filter");
    writeBytes(folder / "Lead.syx", programDump(hardware::kProgramEditBufferDumpType, 0, 1));
    writeBytes(folder / "bank1.syx", programBank(10, 3));
    // A backup holding the same sounds as bank1.
    writeBytes(folder / "Backups" / "Bank 1 backup.syx", programBank(10, 3));
    writeBytes(folder / "Mine" / "Backups" / "not a backup.syx", programDump(hardware::kProgramEditBufferDumpType, 0, 50));

    const auto files = scanLibrary(folder);
    REQUIRE(files.size() == 4);
    for (const auto& file : files)
        CHECK(file.isBackup == (file.name == "Bank 1 backup"));

    auto metadata = LibraryMetadata::load(folder);
    const auto& bank = *std::find_if(files.begin(), files.end(), [](const auto& f) { return f.name == "bank1"; });
    const auto& lead = *std::find_if(files.begin(), files.end(), [](const auto& f) { return f.name == "Lead"; });
    REQUIRE(metadata.set(lead.entries.front(), { "Screamer", "Lead", true }) == MetadataSaveResult::Saved);
    REQUIRE(metadata.set(bank.entries[0], { {}, "Bass", false }) == MetadataSaveResult::Saved);
    REQUIRE(metadata.set(bank.entries[2], { "Sub", "Bass", true }) == MetadataSaveResult::Saved);

    CHECK_FALSE(LibraryFilter {}.isActive());
    CHECK(filterLibrary(files, metadata, {}).size() == 3 + 1 + 1);
    CHECK(names(filterLibrary(files, metadata, { "Bass", false }), metadata)
          == std::vector<std::string> { "Bank 1 \xE2\x80\x93 1", "Sub" });
    CHECK(names(filterLibrary(files, metadata, { {}, true }), metadata) == std::vector<std::string> { "Screamer", "Sub" });
    CHECK(names(filterLibrary(files, metadata, { "Bass", true }), metadata) == std::vector<std::string> { "Sub" });
    CHECK(filterLibrary(files, metadata, { "Pad", false }).empty());
}

TEST_CASE("A filtered Library keeps its groups, leaving out those with no matching sound")
{
    const auto folder = freshFolder("filter-groups");
    writeBytes(folder / "Lead.syx", programDump(hardware::kProgramEditBufferDumpType, 0, 1));
    writeBytes(folder / "Factory" / "bank1.syx", programBank(10, 3));
    writeBytes(folder / "My sounds" / "Pad.syx", programDump(hardware::kProgramEditBufferDumpType, 0, 20));
    writeBytes(folder / "My sounds" / "Live" / "Encore.syx", programDump(hardware::kProgramEditBufferDumpType, 0, 30));
    writeBytes(folder / "Backups" / "Bank 1 backup.syx", programBank(10, 3));

    const auto files = scanLibrary(folder);
    const auto fileNamed = [&files](const std::string& name) -> const LibraryFile& {
        return *std::find_if(files.begin(), files.end(), [&name](const auto& f) { return f.name == name; });
    };
    auto metadata = LibraryMetadata::load(folder);
    REQUIRE(metadata.set(fileNamed("Lead").entries.front(), { {}, "Lead", true }) == MetadataSaveResult::Saved);
    REQUIRE(metadata.set(fileNamed("bank1").entries[1], { {}, "Bass", true }) == MetadataSaveResult::Saved);
    REQUIRE(metadata.set(fileNamed("Encore").entries.front(), { {}, "Bass", false }) == MetadataSaveResult::Saved);

    SECTION("favourites only: the top-level sound and the Factory group")
    {
        const auto shown = groupEntries(filterLibrary(files, metadata, { {}, true }));
        CHECK(shown.name.empty());
        REQUIRE(shown.entries.size() == 1);
        CHECK(shown.entries[0].file->name == "Lead");
        REQUIRE(shown.groups.size() == 1);
        CHECK(shown.groups[0].name == "Factory");
        CHECK(shown.groups[0].groups.empty());
        REQUIRE(shown.groups[0].entries.size() == 1);
        CHECK(shown.groups[0].entries[0].entry == &fileNamed("bank1").entries[1]);
    }

    SECTION("one category: a nested group keeps its parents, with no sounds of their own")
    {
        const auto shown = groupEntries(filterLibrary(files, metadata, { "Bass", false }));
        CHECK(shown.entries.empty());
        REQUIRE(shown.groups.size() == 2);
        CHECK(shown.groups[0].name == "Factory");
        const auto& mine = shown.groups[1];
        CHECK(mine.name == "My sounds");
        CHECK(mine.entries.empty());
        REQUIRE(mine.groups.size() == 1);
        CHECK(mine.groups[0].name == "Live");
        REQUIRE(mine.groups[0].entries.size() == 1);
        CHECK(mine.groups[0].entries[0].file->name == "Encore");
    }

    SECTION("nothing matching: no groups at all")
    {
        const auto shown = groupEntries(filterLibrary(files, metadata, { "Pad", false }));
        CHECK(shown.entries.empty());
        CHECK(shown.groups.empty());
    }
}

TEST_CASE("Auto-tag writes categories marked auto-assigned, leaving sounds that have a category alone")
{
    const auto folder = freshFolder("auto-tag");
    const auto lead = programEntry(values(kProgramParameterCount, 1));
    const auto named = programEntry(values(kProgramParameterCount, 2));
    const auto plain = programEntry(values(kProgramParameterCount, 3));
    auto metadata = LibraryMetadata::load(folder);
    REQUIRE(metadata.set(lead, { "Screamer", "Lead", false }) == MetadataSaveResult::Saved);
    REQUIRE(metadata.set(named, { "Bee", {}, true }) == MetadataSaveResult::Saved);

    REQUIRE(metadata.autoAssign({ { SoundFingerprint::of(lead), "Bass" },
                                  { SoundFingerprint::of(named), "Pad" },
                                  { SoundFingerprint::of(plain), "Keys" } })
            == MetadataSaveResult::Saved);
    const auto reloaded = LibraryMetadata::load(folder);
    CHECK(reloaded.info(lead) == SoundInfo { "Screamer", "Lead", false, false });
    CHECK(reloaded.info(named) == SoundInfo { "Bee", "Pad", true, true });
    CHECK(reloaded.info(plain) == SoundInfo { {}, "Keys", false, true });
    CHECK(reloaded.autoAssignedCount() == 2);
    CHECK_THAT(readText(folder / "library.json"), ContainsSubstring("\"category\": \"Keys\", \"autoAssigned\": true"));

    SECTION("a category set on disk by another window meanwhile is not replaced")
    {
        auto other = LibraryMetadata::load(folder);
        const auto late = programEntry(values(kProgramParameterCount, 4));
        REQUIRE(other.set(late, { {}, "FX", false }) == MetadataSaveResult::Saved);
        REQUIRE(metadata.autoAssign({ { SoundFingerprint::of(late), "Bass" } }) == MetadataSaveResult::Saved);
        CHECK(LibraryMetadata::load(folder).info(late) == SoundInfo { {}, "FX", false, false });
    }
}

TEST_CASE("Setting a category by hand makes it the user's; other edits keep the auto-assigned mark")
{
    const auto folder = freshFolder("auto-tag-hand");
    const auto a = programEntry(values(kProgramParameterCount, 1));
    const auto b = programEntry(values(kProgramParameterCount, 2));
    const auto c = programEntry(values(kProgramParameterCount, 3));
    auto metadata = LibraryMetadata::load(folder);
    REQUIRE(metadata.autoAssign({ { SoundFingerprint::of(a), "Bass" },
                                  { SoundFingerprint::of(b), "Bass" },
                                  { SoundFingerprint::of(c), "Bass" } })
            == MetadataSaveResult::Saved);

    // Another category picked by hand.
    REQUIRE(metadata.set(a, { {}, "Lead", false, true }) == MetadataSaveResult::Saved);
    CHECK(metadata.info(a) == SoundInfo { {}, "Lead", false, false });
    // A name and a favourite flag leave the category as it was, still auto-assigned.
    REQUIRE(metadata.set(b, { "Wobble", "Bass", true, true }) == MetadataSaveResult::Saved);
    CHECK(metadata.info(b) == SoundInfo { "Wobble", "Bass", true, true });
    // The same category kept by hand.
    REQUIRE(metadata.set(c, { {}, "Bass", false, false }) == MetadataSaveResult::Saved);
    CHECK(metadata.info(c) == SoundInfo { {}, "Bass", false, false });
    // set() never marks a category auto-assigned on its own.
    const auto d = programEntry(values(kProgramParameterCount, 4));
    REQUIRE(metadata.set(d, { {}, "Pad", false, true }) == MetadataSaveResult::Saved);
    CHECK_FALSE(metadata.info(d).autoAssigned);
    CHECK(metadata.autoAssignedCount() == 1);
}

TEST_CASE("Undo auto-tag removes only the auto-assigned categories the user has not touched")
{
    const auto folder = freshFolder("auto-tag-undo");
    const auto mine = programEntry(values(kProgramParameterCount, 1));
    const auto changed = programEntry(values(kProgramParameterCount, 2));
    const auto named = programEntry(values(kProgramParameterCount, 3));
    const auto untouched = programEntry(values(kProgramParameterCount, 4));
    auto metadata = LibraryMetadata::load(folder);
    REQUIRE(metadata.set(mine, { {}, "Lead", false }) == MetadataSaveResult::Saved);
    REQUIRE(metadata.autoAssign({ { SoundFingerprint::of(changed), "Bass" },
                                  { SoundFingerprint::of(named), "Pad" },
                                  { SoundFingerprint::of(untouched), "Keys" } })
            == MetadataSaveResult::Saved);
    REQUIRE(metadata.set(changed, { {}, "FX", false, true }) == MetadataSaveResult::Saved);
    REQUIRE(metadata.set(named, { "Halo", "Pad", false, true }) == MetadataSaveResult::Saved);

    REQUIRE(metadata.removeAutoAssigned() == MetadataSaveResult::Saved);
    const auto reloaded = LibraryMetadata::load(folder);
    CHECK(reloaded.info(mine) == SoundInfo { {}, "Lead", false, false });
    CHECK(reloaded.info(changed) == SoundInfo { {}, "FX", false, false });
    CHECK(reloaded.info(named) == SoundInfo { "Halo", {}, false, false });
    CHECK(reloaded.info(untouched).isEmpty());
    CHECK(reloaded.autoAssignedCount() == 0);
    CHECK(reloaded.taggedSounds().size() == 3);
}

TEST_CASE("Auto-tag and its undo never write a file that cannot be read")
{
    const auto folder = freshFolder("auto-tag-unreadable");
    const std::string damaged = "{ \"format\": \"NL2X Editor Library\", \"version\": 1, \"sounds\": [ { \"id\": ";
    const std::string wrongMark = R"({ "format": "NL2X Editor Library", "version": 1, "sounds": [ { "id": "s1", "kind": "program", "values": ")"
                                  + std::string(kProgramParameterCount * 2, '0')
                                  + R"(", "category": "Bass", "autoAssigned": "yes" } ] })";
    const auto entry = programEntry(values(kProgramParameterCount, 1));
    for (const auto& text : { damaged, wrongMark }) {
        writeText(folder / "library.json", text);
        auto metadata = LibraryMetadata::load(folder);
        CHECK(metadata.status() == LibraryMetadataStatus::Unreadable);
        CHECK(metadata.autoAssign({ { SoundFingerprint::of(entry), "Bass" } }) == MetadataSaveResult::RefusedUnreadable);
        CHECK(metadata.removeAutoAssigned() == MetadataSaveResult::RefusedUnreadable);
        CHECK(readText(folder / "library.json") == text);
    }
}
