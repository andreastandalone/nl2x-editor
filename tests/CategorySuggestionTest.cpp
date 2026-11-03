// Suggested categories for Library sounds: Percussion for every Percussion Kit, the category a
// keyword in a single sound's file name says, and Bass or Keys from clear parameters, never any
// other category from parameters. The Programs here are synthetic, built with the values that
// make each case.

#include "nl2x/CategorySuggestion.h"
#include "nl2x/HardwareAssumptions.h"
#include "nl2x/Library.h"
#include "nl2x/Performance.h"
#include "nl2x/SysEx.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
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
    // Stored 2: what pads, keys and strings hold (CategorySuggestion.cpp).
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

std::optional<CategorySuggestion> suggestionForFile(const std::string& name, const Program& program = plainSound())
{
    const auto file = singleProgramFile(name, program);
    REQUIRE(file.entries.size() == 1);
    return suggestCategory(file, file.entries.front());
}

CategorySuggestion byName(const std::string& category)
{
    return { category, CategorySuggestionSource::Name };
}

CategorySuggestion byParameters(const std::string& category)
{
    return { category, CategorySuggestionSource::Parameters };
}

} // namespace

TEST_CASE("Every Percussion Kit is suggested as Percussion, whatever its name")
{
    PercussionKit kit;
    kit.fill(keysSound());
    const auto file = recogniseLibraryFile("Warm Pad", toSysEx(PercussionKitDump { 0, 0, 16, kit }));
    REQUIRE(file.entries.size() == 1);
    CHECK(suggestCategory(file, file.entries.front())
          == CategorySuggestion { "Percussion", CategorySuggestionSource::PercussionKit });
}

TEST_CASE("Performances get no suggestion")
{
    const auto bytes = buildSysEx(0, hardware::kPerformanceEditBufferDumpType, 0,
                                  std::vector<std::int8_t>(kPerformanceParameterCount, 0));
    const auto file = recogniseLibraryFile("Bass Split", bytes);
    REQUIRE(file.entries.size() == 1);
    CHECK_FALSE(suggestCategory(file, file.entries.front()));
}

TEST_CASE("A keyword in a single Program's file name suggests its category")
{
    CHECK(suggestionForFile("Fat Bass") == byName("Bass"));
    CHECK(suggestionForFile("sawbass_2") == byName("Bass"));
    CHECK(suggestionForFile("Warm Pad") == byName("Pad"));
    CHECK(suggestionForFile("Choir") == byName("Pad"));
    CHECK(suggestionForFile("Church Organ") == byName("Keys"));
    CHECK(suggestionForFile("Tubular Bells") == byName("Keys"));
    CHECK(suggestionForFile("Nylon Guitar") == byName("Keys"));
    CHECK(suggestionForFile("Synth Brass 3") == byName("Brass/Strings"));
    CHECK(suggestionForFile("Solo Cello") == byName("Brass/Strings"));
    CHECK(suggestionForFile("Bassoon") == byName("Brass/Strings"));
    CHECK(suggestionForFile("Sync Lead") == byName("Lead"));
    CHECK(suggestionForFile("Cowbell") == byName("Percussion"));
    CHECK(suggestionForFile("Laser Zap") == byName("FX"));
}

TEST_CASE("The last keyword of a name is the sound")
{
    CHECK(suggestionForFile("String Pad") == byName("Pad"));
    CHECK(suggestionForFile("Arpeggio Bass") == byName("Bass"));
    CHECK(suggestionForFile("Bell Piano") == byName("Keys"));
    CHECK(suggestionForFile("Pad Bells") == byName("Keys"));
    // "Phone" inside an instrument's name is not a telephone.
    CHECK(suggestionForFile("Saxophone") == byName("Brass/Strings"));
    CHECK(suggestionForFile("Vibraphone") == byName("Keys"));
}

TEST_CASE("A Lead Voice is a Lead, a voice alone a Pad")
{
    CHECK(suggestionForFile("Lead Voice 3") == byName("Lead"));
    CHECK(suggestionForFile("LeadVoice01") == byName("Lead"));
    CHECK(suggestionForFile("Voice of Angels") == byName("Pad"));
}

TEST_CASE("A name that joins two categories, or names none, suggests nothing by name")
{
    CHECK_FALSE(suggestionForFile("Bass/Lead"));
    CHECK_FALSE(suggestionForFile("Mimiac"));
    CHECK_FALSE(suggestionForFile("1301Sync01"));
    // An arpeggio is how a sound is played, not a category.
    CHECK_FALSE(suggestionForFile("Arp Seq 4"));
    // Keywords that are only words of their own.
    CHECK_FALSE(suggestionForFile("Subtone"));
    CHECK_FALSE(suggestionForFile("Epic"));
}

TEST_CASE("The name wins over the parameters")
{
    CHECK(suggestionForFile("Slap Bass", keysSound()) == byName("Bass"));
    CHECK(suggestionForFile("Rhodes", bassSound()) == byName("Keys"));
}

TEST_CASE("Clear parameters suggest Bass or Keys when the name says nothing")
{
    CHECK(suggestionForFile("Mimiac", bassSound()) == byParameters("Bass"));
    CHECK(suggestionForFile("Mimiac", keysSound()) == byParameters("Keys"));
    CHECK(suggestCategory(bassSound()) == byParameters("Bass"));
    CHECK(suggestCategory(keysSound()) == byParameters("Keys"));
}

TEST_CASE("Parameters never suggest Pad, Lead or anything but Bass and Keys")
{
    CHECK_FALSE(suggestCategory(padSound()));
    CHECK_FALSE(suggestCategory(leadSound()));
    CHECK_FALSE(suggestCategory(plainSound()));
    CHECK_FALSE(suggestCategory(Program()));
}

TEST_CASE("A Bass with the arpeggiator on and lead-like settings is too unsure to suggest")
{
    auto unsure = bassSound();
    unsure.setValue(P::OctaveShift, 2);
    unsure.setValue(P::PortamentoTime, 50);
    unsure.setValue(P::Lfo2DestinationArpMode, 1);
    unsure.setValue(P::Lfo2Amount, 60);
    CHECK_FALSE(suggestCategory(unsure));
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
    CHECK(suggestCategory(bank, bank.entries[0]) == byParameters("Bass"));
    CHECK_FALSE(suggestCategory(bank, bank.entries[1]));
}
