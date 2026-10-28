// Programs and Percussion Kits to and from SysEx dumps: byte-for-byte round-trips, values outside
// the documented ranges kept as they are (research §3). Fixtures are generated here from values we
// chose; factory data is tested in FactoryBankTest.cpp only when present locally.

#include "nl2x/Program.h"
#include "nl2x/ProgramParameters.h"
#include "nl2x/SysEx.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <set>
#include <string_view>
#include <vector>

using namespace nl2x;

namespace {

using Bytes = std::vector<std::uint8_t>;

// Every parameter at a different value, many outside their documented ranges (−128 in Osc2
// Semitones, 127 in Filter Type…).
Program fixtureProgram(int seed)
{
    Program::Values values;
    for (std::size_t i = 0; i < values.size(); ++i)
        values[i] = static_cast<std::int8_t>(static_cast<std::uint8_t>(i * 59 + static_cast<std::size_t>(seed) * 17 + 128));
    return Program(values);
}

// A dump as another tool might write it: header bytes we would not choose ourselves.
Bytes fixtureProgramDumpBytes()
{
    return buildSysEx(0x0F, 3, 42, fixtureProgram(1).bytes());
}

} // namespace

TEST_CASE("Program dump: reading and writing back gives the same bytes")
{
    const auto bytes = fixtureProgramDumpBytes();
    REQUIRE(bytes.size() == kProgramDumpSize);

    const auto dump = parseProgramDump(bytes);
    REQUIRE(dump);
    CHECK(dump->globalChannel == 0x0F);
    CHECK(dump->type == 3);
    CHECK(dump->spec == 42);
    CHECK(dump->program == fixtureProgram(1));
    CHECK(toSysEx(*dump) == bytes);
}

TEST_CASE("Program dump: values outside the documented ranges are preserved, never clamped")
{
    Program program;
    program.setValue(ProgramParameter::Osc2Semitones, 127);   // documented 0–120
    program.setValue(ProgramParameter::FilterCutoff, -1);     // documented 0–127
    program.setValue(ProgramParameter::VoiceMode, 3);         // documented 0–2, found in [OFF-FB]
    program.setValue(ProgramParameter::ModWheelDestination, 7); // documented 0–4, found in [OFF-FB]
    program.setValue(ProgramParameter::FilterCutoffMorphAmount, -128);

    const ProgramDump dump { 0, 0, 1, program };
    const auto parsed = parseProgramDump(toSysEx(dump));

    REQUIRE(parsed);
    CHECK(parsed->program.value(ProgramParameter::Osc2Semitones) == 127);
    CHECK(parsed->program.value(ProgramParameter::FilterCutoff) == -1);
    CHECK(parsed->program.value(ProgramParameter::VoiceMode) == 3);
    CHECK(parsed->program.value(ProgramParameter::ModWheelDestination) == 7);
    CHECK(parsed->program.value(ProgramParameter::FilterCutoffMorphAmount) == -128);
    CHECK(*parsed == dump);
}

TEST_CASE("Program dump: every one of the 256 byte values survives in every parameter position")
{
    for (int seed = 0; seed < 256; ++seed) {
        const ProgramDump dump { 5, 0, 2, fixtureProgram(seed) };
        const auto bytes = toSysEx(dump);
        const auto parsed = parseProgramDump(bytes);
        REQUIRE(parsed);
        REQUIRE(toSysEx(*parsed) == bytes);
    }
}

TEST_CASE("Program dump: anything but a complete, nibble-coded 139-byte dump is refused")
{
    auto bytes = fixtureProgramDumpBytes();
    CHECK_FALSE(parseProgramDump(Bytes(bytes.begin(), bytes.end() - 2)));

    auto corrupt = bytes;
    corrupt[kSysExHeaderSize] = 0x10; // not a nibble
    CHECK_FALSE(parseProgramDump(corrupt));

    auto otherMaker = bytes;
    otherMaker[1] = 0x41;
    CHECK_FALSE(parseProgramDump(otherMaker));

    CHECK_FALSE(parsePercussionKitDump(bytes));
}

TEST_CASE("Percussion Kit dump: eight Programs, read and written back byte for byte")
{
    PercussionKit kit;
    for (std::size_t i = 0; i < kit.size(); ++i)
        kit[i] = fixtureProgram(static_cast<int>(i) + 10);
    const PercussionKitDump dump { 0x0F, 2, 104, kit };

    const auto bytes = toSysEx(dump);
    REQUIRE(bytes.size() == kPercussionKitDumpSize);

    const auto parsed = parsePercussionKitDump(bytes);
    REQUIRE(parsed);
    CHECK(*parsed == dump);
    CHECK(parsed->kit[7] == fixtureProgram(17));
    CHECK(toSysEx(*parsed) == bytes);
    CHECK_FALSE(parseProgramDump(bytes));
}

TEST_CASE("A .syx file is split into its complete messages")
{
    const auto first = fixtureProgramDumpBytes();
    const auto second = buildRequest(0, 14, 1);

    Bytes file { 0x00 };
    file.insert(file.end(), first.begin(), first.end());
    file.insert(file.end(), { 0xF0, 0x33, 0x00 }); // cut off by the next message
    file.insert(file.end(), second.begin(), second.end());
    file.insert(file.end(), { 0xF0, 0x33 });       // unterminated at the end

    const auto messages = splitSysExMessages(file);
    REQUIRE(messages.size() == 2);
    CHECK(messages[0] == first);
    CHECK(messages[1] == second);
}

// ---------------------------------------------------------------------------------------------
// The parameter table.

TEST_CASE("Parameter table: one entry per Program offset, with unique keys and names")
{
    const auto table = programParameters();
    REQUIRE(table.size() == kProgramParameterCount);

    std::set<std::string_view> keys;
    std::set<std::string_view> names;
    for (std::size_t offset = 0; offset < table.size(); ++offset) {
        const auto& info = table[offset];
        CHECK(info.sysExOffset == offset);
        CHECK(&programParameterInfo(info.id) == &info);
        CHECK(info.minValue < info.maxValue);
        keys.insert(info.key);
        names.insert(info.name);
    }
    CHECK(keys.size() == kProgramParameterCount);
    CHECK(names.size() == kProgramParameterCount);
}

TEST_CASE("Parameter table: 25 Morph Amounts at offsets 25–49, each paired with its base parameter")
{
    int morphAmounts = 0;
    for (const auto& info : programParameters()) {
        if (info.morphedParameter) {
            ++morphAmounts;
            CHECK(info.sysExOffset >= 25);
            CHECK(info.sysExOffset <= 49);
            CHECK(info.minValue == -128);
            CHECK(info.maxValue == 127);
            CHECK(programParameterInfo(*info.morphedParameter).morphAmount == info.id);
        }
        if (info.morphAmount)
            CHECK(programParameterInfo(*info.morphAmount).morphedParameter == info.id);
    }
    CHECK(morphAmounts == 25);
    CHECK(programParameterInfo(ProgramParameter::FilterCutoff).morphAmount == ProgramParameter::FilterCutoffMorphAmount);
    CHECK(programParameterInfo(ProgramParameter::FilterCutoffMorphAmount).name == "Filter Cutoff Morph Amount");
}

TEST_CASE("Parameter table: the released identity of Filter Cutoff is unchanged")
{
    const auto& cutoff = programParameterInfo(ProgramParameter::FilterCutoff);
    CHECK(cutoff.key == "filterCutoff");
    CHECK(cutoff.cc == 74);
    CHECK(cutoff.sysExOffset == 3);
    CHECK(programParameterInfo(ProgramParameter::Osc2Semitones).maxValue == 120);
    CHECK(programParameterInfo(ProgramParameter::Lfo2DestinationArpMode).maxValue == 8);
}
