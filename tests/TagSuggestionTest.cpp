// Suggested tags for Library sounds: Percussion for every Percussion Kit, the tags keywords in a
// single sound's file name say, any tag the user's own hand-set tags teach (nearest neighbours,
// each tag on its own), and Bass or Keys from clear parameters when nothing else suggests a tag.
// The Programs here are synthetic, built with the values that make each case.

#include "nl2x/HardwareAssumptions.h"
#include "nl2x/Library.h"
#include "nl2x/LibraryMetadata.h"
#include "nl2x/Performance.h"
#include "nl2x/SysEx.h"
#include "nl2x/TagSuggestion.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

using namespace nl2x;
using P = ProgramParameter;

namespace {

// A sound that suggests nothing from its parameters: poly, a medium attack and release on a held
// note, filter attack, as many soft brass and string sounds are.
Program plainSound()
{
    Program program;
    program.setValue(P::Osc2Semitones, 60);
    program.setValue(P::OscMix, 30);
    program.setValue(P::FilterCutoff, 70);
    program.setValue(P::FilterEnvAmount, 50);
    program.setValue(P::FilterEnvAttack, 40);
    program.setValue(P::AmpEnvAttack, 30);
    program.setValue(P::AmpEnvDecay, 80);
    program.setValue(P::AmpEnvSustain, 110);
    program.setValue(P::AmpEnvRelease, 50);
    program.setValue(P::ModEnvAmount, 64);
    program.setValue(P::FilterKeyboardTrack, 2);
    // Stored 2: what pads, keys and strings hold (TagSuggestion.cpp).
    program.setValue(P::VoiceMode, 2);
    program.setValue(P::OctaveShift, 2);
    program.setValue(P::Lfo2DestinationArpMode, 3);
    return program;
}

// A mono sound two octaves down with a snappy filter.
Program bassSound()
{
    auto program = plainSound();
    program.setValue(P::VoiceMode, 0);
    program.setValue(P::OctaveShift, 0);
    program.setValue(P::AmpEnvAttack, 0);
    program.setValue(P::AmpEnvRelease, 10);
    program.setValue(P::FilterCutoff, 20);
    program.setValue(P::FilterEnvAmount, 80);
    program.setValue(P::FilterEnvAttack, 0);
    return program;
}

// A poly sound with no attack that decays to little sustain, as an electric piano does.
Program keysSound()
{
    auto program = plainSound();
    program.setValue(P::AmpEnvAttack, 0);
    program.setValue(P::AmpEnvDecay, 90);
    program.setValue(P::AmpEnvSustain, 10);
    program.setValue(P::AmpEnvRelease, 30);
    program.setValue(P::FilterEnvAttack, 0);
    return program;
}

// A slow swell held with a long release.
Program padSound()
{
    auto program = plainSound();
    program.setValue(P::AmpEnvAttack, 90);
    program.setValue(P::AmpEnvRelease, 100);
    program.setValue(P::FilterEnvAttack, 0);
    program.setValue(P::Lfo1Amount, 80);
    return program;
}

// Mono, at pitch, with portamento.
Program leadSound()
{
    auto program = plainSound();
    program.setValue(P::VoiceMode, 1);
    program.setValue(P::PortamentoTime, 40);
    program.setValue(P::AmpEnvAttack, 0);
    program.setValue(P::FilterEnvAttack, 0);
    return program;
}

LibraryFile singleProgramFile(const std::string& name, const Program& program)
{
    return recogniseLibraryFile(name, toSysEx(ProgramDump { 0, hardware::kProgramEditBufferDumpType, 0, program }));
}

std::vector<TagSuggestion> suggestionsForFile(const std::string& name, const Program& program = plainSound())
{
    const auto file = singleProgramFile(name, program);
    REQUIRE(file.entries.size() == 1);
    return suggestTags(file, file.entries.front());
}

std::vector<TagSuggestion> from(TagSuggestionSource source, const std::vector<std::string>& tags)
{
    std::vector<TagSuggestion> suggestions;
    for (const auto& tag : tags)
        suggestions.push_back({ tag, source });
    return suggestions;
}

std::vector<TagSuggestion> byName(const std::vector<std::string>& tags)
{
    return from(TagSuggestionSource::Name, tags);
}

std::vector<TagSuggestion> byParameters(const std::string& tag)
{
    return from(TagSuggestionSource::Parameters, { tag });
}

using Tags = std::vector<std::string>;

} // namespace

TEST_CASE("Every Percussion Kit is suggested as Percussion, whatever its name")
{
    PercussionKit kit;
    kit.fill(keysSound());
    const auto file = recogniseLibraryFile("Warm Pad", toSysEx(PercussionKitDump { 0, 0, 16, kit }));
    REQUIRE(file.entries.size() == 1);
    CHECK(suggestTags(file, file.entries.front()) == from(TagSuggestionSource::PercussionKit, { "Percussion" }));
}

TEST_CASE("Performances get no suggestion")
{
    const auto bytes = buildSysEx(0, hardware::kPerformanceEditBufferDumpType, 0,
                                  std::vector<std::int8_t>(kPerformanceParameterCount, 0));
    const auto file = recogniseLibraryFile("Bass Split", bytes);
    REQUIRE(file.entries.size() == 1);
    CHECK(suggestTags(file, file.entries.front()).empty());
}

TEST_CASE("Keywords in a single Program's file name suggest what the sound is")
{
    CHECK(suggestionsForFile("Fat Bass") == byName({ "Bass" }));
    CHECK(suggestionsForFile("sawbass_2") == byName({ "Bass" }));
    CHECK(suggestionsForFile("Warm Pad") == byName({ "Pad" }));
    CHECK(suggestionsForFile("Choir") == byName({ "Pad" }));
    CHECK(suggestionsForFile("Atmosphere 2") == byName({ "Pad", "Ambient" }));
    CHECK(suggestionsForFile("Church Organ") == byName({ "Keys", "Organ" }));
    CHECK(suggestionsForFile("Rhodes") == byName({ "Keys" }));
    CHECK(suggestionsForFile("Tubular Bells") == byName({ "Bell" }));
    CHECK(suggestionsForFile("Nylon Guitar") == byName({ "Pluck" }));
    CHECK(suggestionsForFile("Pluck 1") == byName({ "Pluck" }));
    CHECK(suggestionsForFile("Solo Cello") == byName({ "Strings" }));
    CHECK(suggestionsForFile("Sync Lead") == byName({ "Lead" }));
    CHECK(suggestionsForFile("Cowbell") == byName({ "Percussion" }));
    CHECK(suggestionsForFile("Laser Zap") == byName({ "FX" }));
}

TEST_CASE("Words that say how a sound plays add their tags wherever they are")
{
    CHECK(suggestionsForFile("Synth Brass 3") == byName({ "Brass", "Synth" }));
    CHECK(suggestionsForFile("Arpeggio Bass") == byName({ "Bass", "Arp" }));
    CHECK(suggestionsForFile("Arp Seq 4") == byName({ "Arp", "Rhythmic" }));
    CHECK(suggestionsForFile("Gated Pad") == byName({ "Pad", "Rhythmic" }));
    CHECK(suggestionsForFile("PolySweep") == byName({ "Poly", "Sweep" }));
    CHECK(suggestionsForFile("Mono Lead") == byName({ "Lead", "Mono" }));
    CHECK(suggestionsForFile("Ambient Bells") == byName({ "Bell", "Ambient" }));
    // Only where they start a word: inside "Harp" there is no Arp.
    CHECK(suggestionsForFile("Harp") == byName({ "Pluck" }));
}

TEST_CASE("The last keyword of a name is the sound")
{
    CHECK(suggestionsForFile("String Pad") == byName({ "Pad" }));
    CHECK(suggestionsForFile("Bell Piano") == byName({ "Keys" }));
    CHECK(suggestionsForFile("Pad Bells") == byName({ "Bell" }));
    CHECK(suggestionsForFile("Pluck Bass") == byName({ "Bass" }));
    // "Phone" inside an instrument's name is not a telephone.
    CHECK(suggestionsForFile("Saxophone") == byName({ "Brass" }));
    CHECK(suggestionsForFile("Vibraphone") == byName({ "Keys" }));
}

TEST_CASE("A Lead Voice is a Lead, a voice alone a Pad")
{
    CHECK(suggestionsForFile("Lead Voice 3") == byName({ "Lead" }));
    CHECK(suggestionsForFile("LeadVoice01") == byName({ "Lead" }));
    CHECK(suggestionsForFile("Voice of Angels") == byName({ "Pad" }));
}

TEST_CASE("A name that joins two kinds of sound, or names none, says nothing of what the sound is")
{
    CHECK(suggestionsForFile("Bass/Lead").empty());
    CHECK(suggestionsForFile("Bass/Lead Arp") == byName({ "Arp" }));
    CHECK(suggestionsForFile("Mimiac").empty());
    CHECK(suggestionsForFile("1301Sync01").empty());
    // No default tag fits a woodwind.
    CHECK(suggestionsForFile("Bassoon").empty());
    // Keywords that are only words of their own.
    CHECK(suggestionsForFile("Subtone").empty());
    CHECK(suggestionsForFile("Epic").empty());
}

TEST_CASE("The name wins over the parameters")
{
    CHECK(suggestionsForFile("Slap Bass", keysSound()) == byName({ "Bass" }));
    CHECK(suggestionsForFile("Rhodes", bassSound()) == byName({ "Keys" }));
    // A name that only says how the sound plays leaves no room for the rules either.
    CHECK(suggestionsForFile("Sweep 7", bassSound()) == byName({ "Sweep" }));
}

TEST_CASE("Clear parameters suggest Bass or Keys when the name says nothing")
{
    CHECK(suggestionsForFile("Mimiac", bassSound()) == byParameters("Bass"));
    CHECK(suggestionsForFile("Mimiac", keysSound()) == byParameters("Keys"));
    CHECK(suggestTags(bassSound()) == byParameters("Bass"));
    CHECK(suggestTags(keysSound()) == byParameters("Keys"));
    CHECK(tagsFromParameters(bassSound()) == Tags { "Bass" });
}

TEST_CASE("Parameters never suggest Pad, Lead or anything but Bass and Keys")
{
    CHECK(suggestTags(padSound()).empty());
    CHECK(suggestTags(leadSound()).empty());
    CHECK(suggestTags(plainSound()).empty());
    CHECK(suggestTags(Program()).empty());
}

TEST_CASE("A Bass with the arpeggiator on and lead-like settings is too unsure to suggest")
{
    auto unsure = bassSound();
    unsure.setValue(P::OctaveShift, 2);
    unsure.setValue(P::PortamentoTime, 50);
    unsure.setValue(P::Lfo2DestinationArpMode, 1);
    unsure.setValue(P::Lfo2Amount, 60);
    CHECK(suggestTags(unsure).empty());
}

TEST_CASE("The entries of a Bank file are judged by their parameters only")
{
    std::vector<std::uint8_t> bytes;
    for (const auto& program : { bassSound(), padSound() }) {
        const auto dump = toSysEx(ProgramDump { 0, hardware::kFirstProgramBankDumpType, 0, program });
        bytes.insert(bytes.end(), dump.begin(), dump.end());
    }
    const auto bank = recogniseLibraryFile("Warm Pads", bytes);
    REQUIRE(bank.entries.size() == 2);
    CHECK(suggestTags(bank, bank.entries[0]) == byParameters("Bass"));
    CHECK(suggestTags(bank, bank.entries[1]).empty());
}

namespace {

// `count` variations of `program`, each a little further from it in a few values, as a user's own
// sounds of one kind are. They differ in more values than an edit of one sound
// (kSimilarSoundMaxDifferences), so that library.json keeps them as different sounds.
std::vector<Program> variations(const Program& program, int count)
{
    std::vector<Program> result;
    for (int i = 0; i < count; ++i) {
        auto variation = program;
        for (const auto parameter : { P::FilterCutoff, P::FilterResonance, P::FilterEnvDecay, P::Lfo1Rate,
                                      P::ModEnvDecay, P::Osc2FineTune })
            variation.setValue(parameter, program.value(parameter) + i);
        result.push_back(variation);
    }
    return result;
}

std::vector<LearnedTags::Example> tagged(const Program& program, int count, const Tags& tags)
{
    std::vector<LearnedTags::Example> examples;
    for (const auto& each : variations(program, count))
        examples.push_back({ each, tags });
    return examples;
}

std::vector<LearnedTags::Example> joined(std::vector<LearnedTags::Example> a, const std::vector<LearnedTags::Example>& b)
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

std::vector<TagSuggestion> learned(const Tags& tags)
{
    return from(TagSuggestionSource::Learned, tags);
}

} // namespace

TEST_CASE("Programs the user tagged by hand teach their tags, Pad included")
{
    const LearnedTags learnedFrom(joined(joined(tagged(padSound(), 10, { "Pad" }), tagged(keysSound(), 10, { "Keys" })),
                                         tagged(leadSound(), 10, { "Lead" })));
    auto pad = padSound();
    pad.setValue(P::FilterResonance, 20);
    CHECK(suggestTags(pad, learnedFrom) == learned({ "Pad" }));
    CHECK(suggestTags(leadSound(), learnedFrom) == learned({ "Lead" }));
    CHECK(suggestTags(pad).empty());

    const auto file = singleProgramFile("Mimiac", pad);
    CHECK(suggestTags(file, file.entries.front(), learnedFrom) == learned({ "Pad" }));
}

TEST_CASE("Each tag is learned on its own: a sound can be taught several")
{
    // Pads the user also calls Ambient; some of them, not enough, Strings too.
    auto examples = tagged(padSound(), 6, { "Pad", "Ambient" });
    examples = joined(examples, tagged(padSound(), 4, { "Ambient", "Pad", "Strings" }));
    examples = joined(examples, tagged(keysSound(), 10, { "Keys" }));
    const LearnedTags learnedFrom(examples);
    const auto tags = learnedFrom.tagsOf(padSound());
    CHECK(tags.size() == 2);
    CHECK(std::find(tags.begin(), tags.end(), "Pad") != tags.end());
    CHECK(std::find(tags.begin(), tags.end(), "Ambient") != tags.end());
    CHECK(learnedFrom.tagsOf(keysSound()) == Tags { "Keys" });
}

TEST_CASE("Learning needs enough tagged Programs with a tag, and neighbours that agree")
{
    // Seven Pads: one short of the agreement needed among the nine nearest.
    CHECK(LearnedTags(joined(tagged(padSound(), 7, { "Pad" }), tagged(keysSound(), 10, { "Keys" })))
              .tagsOf(padSound())
              .empty());
    CHECK(LearnedTags(joined(tagged(padSound(), 8, { "Pad" }), tagged(keysSound(), 10, { "Keys" }))).tagsOf(padSound())
          == Tags { "Pad" });
    // The same kind of sound tagged two ways: no suggestion.
    CHECK(LearnedTags(joined(tagged(padSound(), 5, { "Pad" }), tagged(padSound(), 5, { "Strings" })))
              .tagsOf(padSound())
              .empty());
    // Too few tagged Programs to learn from at all.
    CHECK(LearnedTags(tagged(padSound(), 8, { "Pad" })).tagsOf(padSound()).empty());
}

TEST_CASE("A sound far from every tagged Program is left to the fixed rules")
{
    const LearnedTags padsOnly(tagged(padSound(), 12, { "Pad" }));
    CHECK(padsOnly.tagsOf(padSound()) == Tags { "Pad" });
    CHECK(padsOnly.tagsOf(bassSound()).empty());
    CHECK(suggestTags(bassSound(), padsOnly) == byParameters("Bass"));
}

TEST_CASE("Precedence: Percussion Kit, then the file name and learned tags together, then the fixed rules")
{
    // The user files these basses under a tag of his own.
    const LearnedTags learnedFrom(joined(tagged(bassSound(), 10, { "Acid" }), tagged(padSound(), 10, { "Pad" })));
    CHECK(suggestTags(bassSound(), learnedFrom) == learned({ "Acid" }));
    const auto unnamed = singleProgramFile("Mimiac", bassSound());
    CHECK(suggestTags(unnamed, unnamed.entries.front(), learnedFrom) == learned({ "Acid" }));
    const auto named = singleProgramFile("Warm Pad", bassSound());
    CHECK(suggestTags(named, named.entries.front(), learnedFrom)
          == std::vector<TagSuggestion> { { "Pad", TagSuggestionSource::Name }, { "Acid", TagSuggestionSource::Learned } });

    PercussionKit kit;
    kit.fill(bassSound());
    const auto kitFile = recogniseLibraryFile("Kit", toSysEx(PercussionKitDump { 0, 0, 16, kit }));
    REQUIRE(kitFile.entries.size() == 1);
    CHECK(suggestTags(kitFile, kitFile.entries.front(), learnedFrom)
          == from(TagSuggestionSource::PercussionKit, { "Percussion" }));
}

TEST_CASE("Only tags set by hand in library.json teach; auto-assigned ones do not")
{
    const auto folder = std::filesystem::path(NL2X_TEST_OUTPUT_DIR) / "tag-learning";
    std::filesystem::remove_all(folder);
    auto metadata = LibraryMetadata::load(folder);

    std::vector<LibraryMetadata::AutoAssignment> assignments;
    for (const auto& pad : variations(padSound(), 10))
        assignments.push_back({ SoundFingerprint::of(pad), { "Pad", "Ambient" } });
    REQUIRE(metadata.autoAssign(assignments) == MetadataSaveResult::Saved);
    CHECK(LearnedTags::of(metadata).tagsOf(padSound()).empty());

    // Confirming Pad by hand on each (Ambient stays auto-assigned) makes Pad the user's.
    for (const auto& pad : variations(padSound(), 10)) {
        auto info = metadata.info(SoundFingerprint::of(pad));
        info.confirmTag("Pad");
        REQUIRE(metadata.set(SoundFingerprint::of(pad), info) == MetadataSaveResult::Saved);
    }
    CHECK(LearnedTags::of(LibraryMetadata::load(folder)).tagsOf(padSound()) == Tags { "Pad" });
}
