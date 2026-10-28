// The memory writer (#12): storing a Program, sending and reading whole Banks, through the
// simulated synth. Tests assert on what crosses the MIDI boundary and on files written.

#include "MidiCable.h"
#include "SimulatedNordLead2X.h"

#include "nl2x/Engine.h"
#include "nl2x/HardwareAssumptions.h"
#include "nl2x/Library.h"
#include "nl2x/MemoryWriter.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <vector>

using namespace nl2x;
using namespace nl2x::test;

namespace {

using Bytes = std::vector<std::uint8_t>;

constexpr std::uint8_t kTestRequestTypeOfSysExBank1 = 15;
hardware::Switches bankReading(bool verified)
{
    hardware::Switches switches;
    switches.bankReadingVerified = verified;
    switches.bankRequestTypeOfSysExBank1 = kTestRequestTypeOfSysExBank1;
    return switches;
}

const hardware::Switches kVerified = bankReading(true);
const hardware::Switches kNotVerified = bankReading(false);
constexpr WallClockTime kNow { 2026, 10, 28, 14, 3, 7 };

SimulatedNordLead2X::Program patternProgram(int seed)
{
    SimulatedNordLead2X::Program program {};
    for (std::size_t i = 0; i < program.size(); ++i)
        program[i] = static_cast<std::int8_t>(static_cast<std::uint8_t>(i * 13 + static_cast<std::size_t>(seed) * 7));
    return program;
}

Bytes readFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
}

// A Bank dump to the synth's memory: a Program dump with a Bank type.
bool isBankDump(std::span<const std::uint8_t> message)
{
    const auto parsed = parseSysEx(message);
    return parsed.kind == SysExKind::ProgramDump && parsed.type != hardware::kProgramEditBufferDumpType;
}

bool isBankRequest(std::span<const std::uint8_t> message)
{
    const auto parsed = parseSysEx(message);
    return parsed.kind == SysExKind::Other && parsed.type >= kTestRequestTypeOfSysExBank1
           && parsed.type < kTestRequestTypeOfSysExBank1 + 4;
}

// The engine and the simulated synth cabled both ways, as the shell drives them (see
// EditorAndSynth.h), with the lock chosen by the test and a clean folder for files.
struct Rig {
    explicit Rig(const hardware::Switches& switches, const std::string& testName)
        : engine(toSynth, switches),
          folder(std::filesystem::path(NL2X_TEST_OUTPUT_DIR) / "memory-writer" / testName)
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
        synth.setBankRequestTypeOfSysExBank1(kTestRequestTypeOfSysExBank1);
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

LibraryFile bankFile(int programs, int kits = 0)
{
    Bytes bytes;
    for (int i = 0; i < programs; ++i) {
        // Header bytes as another tool might write them: they must not matter.
        const auto dump = bankProgramDump(3, 7, static_cast<std::uint8_t>((i + 5) % 99), patternProgram(5000 + i));
        bytes.insert(bytes.end(), dump.begin(), dump.end());
    }
    for (int i = 0; i < kits; ++i) {
        std::vector<std::int8_t> kit(kPercussionKitParameterCount, static_cast<std::int8_t>(i));
        const auto dump = buildSysEx(3, 1, static_cast<std::uint8_t>(99 + i), kit);
        bytes.insert(bytes.end(), dump.begin(), dump.end());
    }
    return recogniseLibraryFile("my bank", bytes);
}

} // namespace

TEST_CASE("Memory writer, locked: storing a Program asks first and needs the no-backup consent")
{
    Rig rig(kNotVerified, "store-locked");
    const auto slotB = rig.synth.editBuffer(Slot::B);

    REQUIRE(rig.memory().prepareStoreProgram(Slot::B, 2, 17, rig.backupFolder, kNow, rig.now) == MemoryRefusal::None);
    rig.settle();

    REQUIRE(rig.status().stage == MemoryStage::AwaitingConfirmation);
    const auto plan = *rig.status().plan;
    CHECK(plan.question == "Overwrite Program 17 in Bank 2 on the synth?");
    CHECK(plan.needsNoBackupConsent);
    CHECK_FALSE(plan.backupFile);
    CHECK_FALSE(plan.noBackupWarning.empty());
    CHECK(plan.details.find("select Bank 2 on the synth") != std::string::npos);
    // Locked: nothing was even read, and no backup folder exists.
    CHECK(std::none_of(rig.toSynth.messages().begin(), rig.toSynth.messages().end(), isBankRequest));
    CHECK(rig.backups().empty());

    CHECK(rig.confirm(NoBackupConsent::NotGiven) == MemoryConfirmResult::RefusedNoBackupConsent);
    rig.runFor(2000);
    CHECK(rig.synth.receivedDumpCount(SimulatedNordLead2X::DumpDestination::Bank) == 0);

    CHECK(rig.confirm(NoBackupConsent::Given) == MemoryConfirmResult::Accepted);
    rig.settle();

    CHECK(rig.status().stage == MemoryStage::Finished);
    REQUIRE(rig.synth.receivedDumpCount(SimulatedNordLead2X::DumpDestination::Bank) == 1);
    const auto& dump = rig.synth.receivedDumps().back();
    CHECK(dump.spec == 16);
    CHECK(rig.synth.bankProgram(2, 16) == slotB);
    // Exactly one location changed.
    for (int bank = 0; bank < 4; ++bank)
        for (int location = 0; location < 99; ++location)
            if (bank != 2 || location != 16)
                CHECK(rig.synth.bankProgram(bank, location) == patternProgram(bank * 100 + location));
}

TEST_CASE("Memory writer: nothing is written without the current plan's confirmation")
{
    Rig rig(kNotVerified, "no-confirmation");
    REQUIRE(rig.memory().prepareBankSend(bankFile(99), 1, rig.backupFolder, kNow, rig.now) == MemoryRefusal::None);
    rig.settle();
    REQUIRE(rig.status().plan);
    const auto token = rig.status().plan->token;

    rig.runFor(5000);
    CHECK(rig.memory().confirm(token + 1, NoBackupConsent::Given, rig.now) == MemoryConfirmResult::RefusedNoSuchPlan);
    rig.runFor(1000);
    rig.memory().cancel();
    CHECK(rig.memory().confirm(token, NoBackupConsent::Given, rig.now) == MemoryConfirmResult::RefusedNoSuchPlan);
    rig.runFor(3000);

    CHECK(rig.status().stage == MemoryStage::Idle);
    CHECK(rig.synth.receivedDumpCount(SimulatedNordLead2X::DumpDestination::Bank) == 0);
    CHECK(std::none_of(rig.toSynth.messages().begin(), rig.toSynth.messages().end(), isBankDump));
}

TEST_CASE("Memory writer, locked: sending a Bank without the no-backup consent writes nothing")
{
    Rig rig(kNotVerified, "send-locked");
    REQUIRE(rig.memory().prepareBankSend(bankFile(99), 2, rig.backupFolder, kNow, rig.now) == MemoryRefusal::None);
    rig.settle();
    REQUIRE(rig.status().plan);
    CHECK(rig.status().plan->needsNoBackupConsent);
    CHECK(rig.status().plan->question == "Overwrite Programs 1–99 in Bank 2 on the synth?");

    CHECK(rig.confirm(NoBackupConsent::NotGiven) == MemoryConfirmResult::RefusedNoBackupConsent);
    rig.runFor(3000);
    CHECK(rig.synth.receivedDumpCount(SimulatedNordLead2X::DumpDestination::Bank) == 0);
    CHECK(rig.status().stage == MemoryStage::AwaitingConfirmation);
}

TEST_CASE("Memory writer, verified: a Bank send backs up the Bank, then asks, then sends paced")
{
    Rig rig(kVerified, "send-verified");
    const auto file = bankFile(99, 10);

    REQUIRE(rig.memory().prepareBankSend(file, 2, rig.backupFolder, kNow, rig.now) == MemoryRefusal::None);
    rig.settle();

    // 1. The backup exists before the question, with the Bank's old contents.
    REQUIRE(rig.status().stage == MemoryStage::AwaitingConfirmation);
    const auto plan = *rig.status().plan;
    CHECK_FALSE(plan.needsNoBackupConsent);
    REQUIRE(plan.backupFile);
    CHECK(plan.backupFile->filename() == "Bank 2 backup 2026-10-28 14.03.07.syx");
    CHECK(plan.backupFile->parent_path() == rig.backupFolder);
    CHECK(plan.details.find("10 Percussion Kits are not sent") != std::string::npos);
    const auto backup = openLibraryFile(*plan.backupFile);
    CHECK(backup.kind == LibraryFileKind::ProgramBank);
    REQUIRE(backup.entries.size() == 99);
    for (int location = 0; location < 99; ++location)
        CHECK(std::get<ProgramDump>(backup.entries[static_cast<std::size_t>(location)].dump).program.bytes()
              == patternProgram(200 + location));
    CHECK(rig.synth.receivedDumpCount(SimulatedNordLead2X::DumpDestination::Bank) == 0);

    // 2. Confirmed: sent, every dump after the backup was on disk, paced.
    CHECK(rig.confirm(NoBackupConsent::NotGiven) == MemoryConfirmResult::Accepted);
    rig.settle();
    CHECK(rig.status().stage == MemoryStage::Finished);
    CHECK(rig.status().message.find("Read back: 99 of 99") != std::string::npos);

    REQUIRE(rig.bankDumpTimes.size() == 99);
    CHECK(std::all_of(rig.backupExistedAtBankDump.begin(), rig.backupExistedAtBankDump.end(), [](bool b) { return b; }));
    for (std::size_t i = 1; i < rig.bankDumpTimes.size(); ++i)
        CHECK(rig.bankDumpTimes[i] - rig.bankDumpTimes[i - 1] >= hardware::kBankWriteSpacingMs);
    // By position in the file, never by its type or spec bytes; only Programs are sent.
    for (int location = 0; location < 99; ++location)
        CHECK(rig.synth.bankProgram(2, location) == patternProgram(5000 + location));
    CHECK(rig.synth.bankProgram(1, 0) == patternProgram(100));
    for (const auto& dump : rig.synth.receivedDumps())
        CHECK(dump.kind == SysExKind::ProgramDump);
}

TEST_CASE("Memory writer, verified: no reply in the middle of the backup aborts before anything is written")
{
    Rig rig(kVerified, "backup-fails");
    int bankRequests = 0;
    rig.onSend = [&](std::span<const std::uint8_t> message) {
        if (isBankRequest(message) && ++bankRequests == 50)
            rig.synth.setAnswersSysEx(false);
    };

    REQUIRE(rig.memory().prepareBankSend(bankFile(99), 2, rig.backupFolder, kNow, rig.now) == MemoryRefusal::None);
    rig.settle();

    CHECK(rig.status().stage == MemoryStage::Failed);
    CHECK(rig.status().message.find("Program 50 of Bank 2 did not answer") != std::string::npos);
    CHECK_FALSE(rig.status().plan);
    CHECK(rig.backups().empty());
    rig.synth.setAnswersSysEx(true);
    rig.runFor(3000);
    CHECK(std::none_of(rig.toSynth.messages().begin(), rig.toSynth.messages().end(), isBankDump));
}

TEST_CASE("Memory writer, verified: storing a Program backs up its Bank first and writes one location")
{
    Rig rig(kVerified, "store-verified");
    const auto slotA = rig.synth.editBuffer(Slot::A);

    REQUIRE(rig.memory().prepareStoreProgram(Slot::A, 2, 99, rig.backupFolder, kNow, rig.now) == MemoryRefusal::None);
    rig.settle();
    REQUIRE(rig.status().plan);
    REQUIRE(rig.status().plan->backupFile);
    CHECK(readFile(*rig.status().plan->backupFile).size() == 99 * kProgramDumpSize);

    CHECK(rig.confirm(NoBackupConsent::NotGiven) == MemoryConfirmResult::Accepted);
    rig.settle();

    CHECK(rig.status().stage == MemoryStage::Finished);
    CHECK(rig.bankDumpTimes.size() == 1);
    CHECK(rig.backupExistedAtBankDump == std::vector<bool> { true });
    CHECK(rig.synth.bankProgram(2, 98) == slotA);
    CHECK(rig.synth.bankProgram(2, 97) == patternProgram(297));
    CHECK(rig.status().message.find("Read back: 1 of 1") != std::string::npos);
}

TEST_CASE("Memory writer: a write waits until the synth has the target Bank selected")
{
    Rig rig(kNotVerified, "wrong-bank");
    rig.synth.selectBank(1);

    REQUIRE(rig.memory().prepareStoreProgram(Slot::A, 2, 5, rig.backupFolder, kNow, rig.now) == MemoryRefusal::None);
    rig.settle();
    const auto firstToken = rig.status().plan->token;
    REQUIRE(rig.confirm(NoBackupConsent::Given) == MemoryConfirmResult::Accepted);
    rig.settle();

    // Offered again, with the reason; nothing written anywhere.
    REQUIRE(rig.status().stage == MemoryStage::AwaitingConfirmation);
    CHECK(rig.status().message.find("Bank 1 is selected on the synth, not Bank 2") != std::string::npos);
    CHECK(rig.status().plan->token != firstToken);
    CHECK(rig.synth.receivedDumpCount(SimulatedNordLead2X::DumpDestination::Bank) == 0);
    CHECK(rig.memory().confirm(firstToken, NoBackupConsent::Given, rig.now) == MemoryConfirmResult::RefusedNoSuchPlan);

    rig.synth.selectBank(2);
    REQUIRE(rig.confirm(NoBackupConsent::Given) == MemoryConfirmResult::Accepted);
    rig.settle();
    CHECK(rig.status().stage == MemoryStage::Finished);
    CHECK(rig.synth.bankProgram(2, 4) == rig.synth.editBuffer(Slot::A));
    CHECK(rig.synth.bankProgram(1, 4) == patternProgram(104));
}

TEST_CASE("Memory writer: reading a Bank gives a Program Bank file that round-trips")
{
    Rig rig(kVerified, "read-bank");
    const auto path = rig.folder / "Bank 3.syx";

    REQUIRE(rig.memory().readBank(3, path, ReplaceExisting::No, rig.now) == MemoryRefusal::None);
    rig.settle();

    REQUIRE(rig.status().stage == MemoryStage::Finished);
    CHECK(rig.status().bankFile == path);
    const auto bytes = readFile(path);
    const auto file = openLibraryFile(path);
    CHECK(file.kind == LibraryFileKind::ProgramBank);
    REQUIRE(file.entries.size() == 99);
    CHECK(file.ignoredMessageCount == 0);
    CHECK(toSysEx(file) == bytes);
    for (int location = 0; location < 99; ++location)
        CHECK(std::get<ProgramDump>(file.entries[static_cast<std::size_t>(location)].dump).program.bytes()
              == patternProgram(300 + location));
    CHECK(rig.synth.receivedDumpCount(SimulatedNordLead2X::DumpDestination::Bank) == 0);

    // An existing file is never replaced unless the user agreed.
    CHECK(rig.memory().readBank(3, path, ReplaceExisting::No, rig.now) == MemoryRefusal::FileExists);
}

TEST_CASE("Memory writer, locked: reading a Bank is experimental, and refused where the request type is known wrong")
{
    Rig rig(kNotVerified, "read-locked");
    REQUIRE(rig.memory().readBank(0, rig.folder / "Bank 0.syx", ReplaceExisting::No, rig.now) == MemoryRefusal::None);
    rig.settle();
    CHECK(rig.status().stage == MemoryStage::Finished);
    CHECK(rig.status().message.find("Experimental") != std::string::npos);

    Rig assumed(hardware::Switches {}, "read-assumed");
    // As printed, Bank 3 would be read with type 14, the Slot Edit Buffer request.
    if (hardware::kBankRequestTypeOfSysExBank1 + 3 == hardware::kSlotEditBufferRequestType)
        CHECK(assumed.memory().readBank(3, assumed.folder / "Bank 3.syx", ReplaceExisting::No, assumed.now)
              == MemoryRefusal::RequestTypeCollides);
}

TEST_CASE("Memory writer: refusals")
{
    Rig rig(kVerified, "refusals");
    CHECK(rig.memory().prepareStoreProgram(Slot::A, 4, 1, rig.backupFolder, kNow, rig.now) == MemoryRefusal::NotARamBank);
    CHECK(rig.memory().prepareStoreProgram(Slot::A, 0, 100, rig.backupFolder, kNow, rig.now)
          == MemoryRefusal::ProgramOutOfRange);
    CHECK(rig.memory().prepareStoreProgram(Slot::A, 0, 0, rig.backupFolder, kNow, rig.now)
          == MemoryRefusal::ProgramOutOfRange);
    CHECK(rig.memory().prepareBankSend(bankFile(1), 0, rig.backupFolder, kNow, rig.now) == MemoryRefusal::NotAProgramBank);
    CHECK(rig.memory().prepareBankSend(bankFile(100), 0, rig.backupFolder, kNow, rig.now)
          == MemoryRefusal::TooManyProgramsInFile);
    CHECK(rig.memory().prepareBankSend(bankFile(0, 2), 0, rig.backupFolder, kNow, rig.now)
          == MemoryRefusal::NoProgramsInFile);

    REQUIRE(rig.memory().readBank(0, rig.folder / "a.syx", ReplaceExisting::No, rig.now) == MemoryRefusal::None);
    CHECK(rig.memory().prepareStoreProgram(Slot::A, 0, 1, rig.backupFolder, kNow, rig.now) == MemoryRefusal::Busy);

    MidiCable nowhere;
    Engine unconnected(nowhere, kVerified);
    CHECK(unconnected.memoryWriter().prepareStoreProgram(Slot::A, 0, 1, rig.backupFolder, kNow, 0)
          == MemoryRefusal::NotConnected);
    CHECK(nowhere.messages().empty());
}

TEST_CASE("Memory writer: connecting again stops a Bank send part way")
{
    Rig rig(kNotVerified, "interrupted");
    REQUIRE(rig.memory().prepareBankSend(bankFile(99), 2, rig.backupFolder, kNow, rig.now) == MemoryRefusal::None);
    rig.settle();
    REQUIRE(rig.confirm(NoBackupConsent::Given) == MemoryConfirmResult::Accepted);
    rig.runWhile([&] { return rig.bankDumpTimes.size() < 10; });

    rig.engine.connect(std::nullopt, rig.now);
    rig.runFor(15000);

    CHECK(rig.status().stage == MemoryStage::Failed);
    CHECK(rig.status().message.find("10 of 99 Programs had been written") != std::string::npos);
    CHECK(rig.bankDumpTimes.size() == 10);
}

TEST_CASE("Memory writer: selecting a Program or Performance is refused while the memory is busy")
{
    Rig rig(kNotVerified, "selection-busy");
    REQUIRE(rig.memory().prepareStoreProgram(Slot::A, 2, 5, rig.backupFolder, kNow, rig.now) == MemoryRefusal::None);
    rig.settle();
    REQUIRE(rig.status().stage == MemoryStage::AwaitingConfirmation);
    const auto sentBefore = rig.toSynth.messages().size();

    CHECK(rig.engine.selectProgram(Slot::A, { ProgramSelection::Kind::Program, 1, 3 }, rig.now)
          == SelectionResult::RefusedMemoryBusy);
    CHECK(rig.engine.selectPerformance({ 1, 4 }, rig.now) == SelectionResult::RefusedMemoryBusy);
    rig.runFor(1000);
    for (auto i = sentBefore; i < rig.toSynth.messages().size(); ++i) {
        const auto& message = rig.toSynth.messages()[i];
        CHECK_FALSE((message.size() == 2 && (message[0] & 0xF0) == 0xC0));
    }
    CHECK(rig.synth.selectedBank() == 2);

    rig.memory().cancel();
    CHECK(rig.engine.selectProgram(Slot::A, { ProgramSelection::Kind::Program, 1, 3 }, rig.now)
          == SelectionResult::Sent);
}

TEST_CASE("Memory writer: a sound selected on the synth during a Bank send stops it")
{
    Rig rig(kNotVerified, "selection-on-synth");
    REQUIRE(rig.memory().prepareBankSend(bankFile(99), 2, rig.backupFolder, kNow, rig.now) == MemoryRefusal::None);
    rig.settle();
    REQUIRE(rig.confirm(NoBackupConsent::Given) == MemoryConfirmResult::Accepted);
    rig.runWhile([&] { return rig.bankDumpTimes.size() < 5; });

    // The user turns the dial on the synth: Slot A now plays a Program of Bank 1.
    rig.synth.selectProgramOnPanel(Slot::A, 1, 0);
    rig.runFor(15000);

    CHECK(rig.status().stage == MemoryStage::Failed);
    CHECK(rig.status().message.find("selected on the synth during the write") != std::string::npos);
    CHECK(rig.bankDumpTimes.size() == 5);
    CHECK(rig.synth.bankProgram(1, 10) == patternProgram(110));
}
