// Round-trips every Program and Percussion Kit of the official factory files [OFF-FB]. Those files
// may not be redistributed (ADR 0002), so this test is skipped unless they are present locally
// under docs/research/factory-bank/ (NL2X_FACTORY_BANK_DIR).

#include "nl2x/Program.h"
#include "nl2x/SysEx.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

using namespace nl2x;

namespace {

std::vector<std::uint8_t> readFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
}

} // namespace

TEST_CASE("Factory Banks: every Program and Percussion Kit round-trips byte for byte")
{
    const std::filesystem::path folder = NL2X_FACTORY_BANK_DIR;
    std::error_code error;
    if (! std::filesystem::is_directory(folder, error))
        SKIP("Factory Bank files not present in " << folder.string());

    int programs = 0;
    int kits = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(folder)) {
        if (entry.path().extension() != ".syx")
            continue;
        for (const auto& message : splitSysExMessages(readFile(entry.path()))) {
            INFO(entry.path().filename().string() << ", spec " << int(message.size() > 5 ? message[5] : 0));
            if (message.size() == kProgramDumpSize) {
                const auto dump = parseProgramDump(message);
                REQUIRE(dump);
                CHECK(toSysEx(*dump) == message);
                ++programs;
            } else if (message.size() == kPercussionKitDumpSize) {
                const auto dump = parsePercussionKitDump(message);
                REQUIRE(dump);
                CHECK(toSysEx(*dump) == message);
                ++kits;
            }
        }
    }

    if (programs + kits == 0)
        SKIP("No Program or Percussion Kit dumps found in " << folder.string());
    // Four Banks of 99 Programs and 10 Kits [OFF-FB].
    CHECK(programs == 4 * 99);
    CHECK(kits == 4 * 10);
}
