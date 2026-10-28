// Round-trips every Program, Percussion Kit and Performance of the official factory files [OFF-FB]. Those files
// may not be redistributed (ADR 0002), so this test is skipped unless they are present locally
// under docs/research/factory-bank/ (NL2X_FACTORY_BANK_DIR).

#include "nl2x/HardwareAssumptions.h"
#include "nl2x/Performance.h"
#include "nl2x/Program.h"
#include "nl2x/SysEx.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

using namespace nl2x;

namespace {

std::vector<std::uint8_t> readFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
}

// Every complete message of every .syx file under the factory folder, or nothing when it is absent.
std::vector<std::pair<std::string, std::vector<std::uint8_t>>> factoryMessages()
{
    const std::filesystem::path folder = NL2X_FACTORY_BANK_DIR;
    std::vector<std::pair<std::string, std::vector<std::uint8_t>>> messages;
    std::error_code error;
    if (! std::filesystem::is_directory(folder, error))
        return messages;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(folder))
        if (entry.path().extension() == ".syx")
            for (auto& message : splitSysExMessages(readFile(entry.path())))
                messages.emplace_back(entry.path().filename().string(), std::move(message));
    return messages;
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

TEST_CASE("Factory Banks: every Performance round-trips byte for byte, its four Programs included")
{
    int performances = 0;
    for (const auto& [file, message] : factoryMessages()) {
        if (message.size() != kPerformanceDumpSize)
            continue;
        INFO(file << ", spec " << int(message[5]));
        const auto dump = parsePerformanceDump(message);
        REQUIRE(dump);
        CHECK(toSysEx(*dump) == message);

        // The embedded Programs are the dump's own bytes, in Slot order.
        auto rebuilt = dump->performance;
        for (auto slot : kAllSlots)
            rebuilt.setProgram(slot, dump->performance.program(slot));
        CHECK(rebuilt == dump->performance);

        // Slot Channels are 0–15 or the −1 taken to mean off (HardwareAssumptions.h).
        for (auto slot : kAllSlots) {
            const auto channel = dump->performance.setting(PerformanceSetting::SlotChannel, slot);
            CHECK((channel == hardware::kPerformanceSlotChannelOff || (channel >= 0 && channel <= 15)));
        }
        ++performances;
    }

    if (performances == 0)
        SKIP("No Performance dumps found in " << NL2X_FACTORY_BANK_DIR);
    // One Bank of 100 Performances [OFF-FB].
    CHECK(performances == 100);
}
