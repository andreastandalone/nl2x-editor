#pragma once

// The engine and the simulated synth cabled both ways, as the shell drives them (see
// EditorAndSynth.h), with the memory writer's lock chosen by the test, every RAM Bank location
// filled with a distinct pattern and a clean folder for files: the rig of the memory writer (#12)
// and Librarian (#44) tests.

#include "MidiCable.h"
#include "SimulatedNordLead2X.h"

#include "nl2x/Engine.h"
#include "nl2x/HardwareAssumptions.h"
#include "nl2x/Library.h"
#include "nl2x/MemoryWriter.h"

#include <catch2/catch_test_macros.hpp>

#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <span>
#include <string>
#include <vector>

namespace nl2x::test {

using Bytes = std::vector<std::uint8_t>;

inline constexpr std::uint8_t kTestRequestTypeOfSysExBank1 = 15;
inline hardware::Switches bankReading(bool verified)
{
    hardware::Switches switches;
    switches.bankReadingVerified = verified;
    switches.bankRequestTypeOfSysExBank1 = kTestRequestTypeOfSysExBank1;
    return switches;
}

inline const hardware::Switches kVerified = bankReading(true);
inline const hardware::Switches kNotVerified = bankReading(false);
inline constexpr WallClockTime kNow { 2026, 10, 28, 14, 3, 7 };

inline SimulatedNordLead2X::Program patternProgram(int seed)
{
    SimulatedNordLead2X::Program program {};
    for (std::size_t i = 0; i < program.size(); ++i)
        program[i] = static_cast<std::int8_t>(static_cast<std::uint8_t>(i * 13 + static_cast<std::size_t>(seed) * 7));
    return program;
}

inline Bytes readFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
}

// A Bank dump to the synth's memory: a Program dump with a Bank type.
inline bool isBankDump(std::span<const std::uint8_t> message)
{
    const auto parsed = parseSysEx(message);
    return parsed.kind == SysExKind::ProgramDump && parsed.type != hardware::kProgramEditBufferDumpType;
}

inline bool isBankRequest(std::span<const std::uint8_t> message)
{
    const auto parsed = parseSysEx(message);
    return parsed.kind == SysExKind::Other && parsed.type >= kTestRequestTypeOfSysExBank1
           && parsed.type < kTestRequestTypeOfSysExBank1 + 4;
}

struct Rig {
    // Files go to a clean `testName` folder under `area` in the test output folder.
    Rig(const hardware::Switches& switches, const std::string& testName, const std::string& area = "memory-writer")
        : engine(toSynth, switches),
          folder(std::filesystem::path(NL2X_TEST_OUTPUT_DIR) / area / testName)
    {
        std::filesystem::remove_all(folder);
        backupFolder = folder / "Backups";
        toSynth.plugInto([this](auto message) {
            if (isBankDump(message)) {
                bankDumpTimes.push_back(now);
                backupExistedAtBankDump.push_back(std::filesystem::exists(backupFolder));
            }
            if (onSend)
                onSend(message);
            synth.receive(message, now);
        });
        fromSynth.plugInto([this](auto message) { pending.emplace_back(message.begin(), message.end()); });
        synth.connectMidiOut(&fromSynth);
        for (auto slot : kAllSlots)
            synth.setEditBuffer(slot, patternProgram(100 + static_cast<int>(slotIndex(slot))));
        for (int bank = 0; bank < 4; ++bank)
            for (int location = 0; location < 99; ++location)
                synth.setBankProgram(bank, location, patternProgram(bank * 100 + location));
        synth.selectBank(2);

        engine.connect(std::nullopt, now);
        runWhile([this] {
            const auto status = engine.connectionStatus();
            return status == ConnectionStatus::SearchingGlobalChannel || status == ConnectionStatus::ReadingSlots
                   || engine.performanceStatus() == PerformanceStatus::Reading;
        });
        REQUIRE(engine.connectionStatus() == ConnectionStatus::Connected);
    }

    void tick()
    {
        now += 10;
        while (! pending.empty()) {
            const auto message = pending.front();
            pending.pop_front();
            engine.handleIncomingMidi(message, now);
        }
        engine.advanceTime(now);
    }

    void runWhile(const std::function<bool()>& condition)
    {
        for (int i = 0; i < 100000 && condition(); ++i)
            tick();
        REQUIRE_FALSE(condition());
    }

    void runFor(TimeMs durationMs)
    {
        const auto end = now + durationMs;
        while (now < end)
            tick();
    }

    MemoryWriter& memory() { return engine.memoryWriter(); }
    const MemoryTaskStatus& status() { return engine.memoryWriter().status(); }

    // Runs until the writer waits for a confirmation or has finished.
    void settle()
    {
        runWhile([this] {
            const auto stage = status().stage;
            return stage != MemoryStage::AwaitingConfirmation && stage != MemoryStage::Finished
                   && stage != MemoryStage::Failed && stage != MemoryStage::Idle;
        });
    }

    MemoryConfirmResult confirm(NoBackupConsent consent)
    {
        REQUIRE(status().plan);
        return memory().confirm(status().plan->token, consent, now);
    }

    std::vector<std::filesystem::path> backups() const
    {
        std::vector<std::filesystem::path> files;
        if (std::filesystem::exists(backupFolder))
            for (const auto& entry : std::filesystem::directory_iterator(backupFolder))
                files.push_back(entry.path());
        return files;
    }

    MidiCable toSynth;
    MidiCable fromSynth;
    SimulatedNordLead2X synth;
    Engine engine;
    std::filesystem::path folder;
    std::filesystem::path backupFolder;
    std::deque<Bytes> pending;
    std::vector<TimeMs> bankDumpTimes;
    std::vector<bool> backupExistedAtBankDump;
    std::function<void(std::span<const std::uint8_t>)> onSend;
    TimeMs now = 1000;
};

} // namespace nl2x::test
