// The memory writer (#12): storing a Program, sending and reading whole Banks, through the
// simulated synth. Tests assert on what crosses the MIDI boundary and on files written.

#include "MemoryRig.h"

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

    // With the manual's printed request types, Bank 3 would be read with type 14, the Slot Edit
    // Buffer request: refused.
    auto printed = kNotVerified;
    printed.bankRequestTypeOfSysExBank1 = hardware::kPrintedFirstBankRequestType;
    Rig asPrinted(printed, "read-as-printed");
    CHECK(asPrinted.memory().readBank(3, asPrinted.folder / "Bank 3.syx", ReplaceExisting::No, asPrinted.now)
          == MemoryRefusal::RequestTypeCollides);
}

TEST_CASE("Memory writer: as shipped, Bank reading is verified and reads the Bank shown as 3 with request type 18")
{
    CHECK(hardware::kBankReadingVerified);
    Rig rig(hardware::Switches {}, "shipped");
    CHECK(rig.memory().bankReadingVerified());
    std::vector<Bytes> requests;
    rig.onSend = [&requests](std::span<const std::uint8_t> message) {
        if (isBankRequest(message))
            requests.emplace_back(message.begin(), message.end());
    };

    REQUIRE(rig.memory().readBank(3, rig.folder / "Bank 3.syx", ReplaceExisting::No, rig.now) == MemoryRefusal::None);
    rig.settle();

    CHECK(rig.status().stage == MemoryStage::Finished);
    CHECK(rig.status().message.find("Experimental") == std::string::npos);
    REQUIRE(requests.size() == 99);
    CHECK(requests.front()[4] == 18);
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

TEST_CASE("Memory writer: no Edit Buffer dump goes out between the confirmation and the end of the write")
{
    Rig rig(kVerified, "edits-held");
    REQUIRE(rig.memory().prepareBankSend(bankFile(99), 2, rig.backupFolder, kNow, rig.now) == MemoryRefusal::None);
    rig.settle();

    const auto isEditBufferProgramDump = [](const Bytes& message) {
        const auto parsed = parseSysEx(message);
        return parsed.kind == SysExKind::ProgramDump && parsed.type == hardware::kProgramEditBufferDumpType;
    };
    REQUIRE(rig.confirm(NoBackupConsent::NotGiven) == MemoryConfirmResult::Accepted);
    const auto confirmedAt = rig.toSynth.messages().size();
    // Morph Amount edits right after confirming (before the Bank check) and during the write.
    int amount = 1;
    rig.engine.setMorphAmount(Slot::A, ProgramParameter::FilterCutoff, amount, rig.now);
    while (rig.memory().holdsEditBufferDumps()) {
        rig.tick();
        rig.engine.setMorphAmount(Slot::A, ProgramParameter::FilterCutoff, ++amount % 100, rig.now);
    }
    const auto writeEndedAt = rig.toSynth.messages().size();
    rig.settle();
    REQUIRE(rig.status().stage == MemoryStage::Finished);
    CHECK(rig.status().message.find("Read back: 99 of 99") != std::string::npos);
    CHECK(rig.synth.receivedDumpCount(SimulatedNordLead2X::DumpDestination::Bank) == 99);

    const auto& sent = rig.toSynth.messages();
    CHECK(std::none_of(sent.begin() + static_cast<std::ptrdiff_t>(confirmedAt),
                       sent.begin() + static_cast<std::ptrdiff_t>(writeEndedAt), isEditBufferProgramDump));
    // The held edit goes out afterwards.
    rig.runFor(500);
    CHECK(std::any_of(sent.begin() + static_cast<std::ptrdiff_t>(writeEndedAt), sent.end(), isEditBufferProgramDump));
}

TEST_CASE("Memory writer: a set of Programs is written in ascending order, one dump per location")
{
    Rig rig(kVerified, "program-writes");
    std::vector<ProgramWrite> writes;
    for (int program : { 90, 2, 40, 7, 11, 13, 17, 19, 23, 29, 31, 37 })
        writes.push_back({ program, Program(patternProgram(6000 + program)) });

    CHECK(rig.memory().prepareProgramWrites(2, {}, "x", rig.backupFolder, kNow, rig.now) == MemoryRefusal::NothingToWrite);
    CHECK(rig.memory().prepareProgramWrites(2, { { 3, {} }, { 3, {} } }, "x", rig.backupFolder, kNow, rig.now)
          == MemoryRefusal::DuplicateProgram);
    CHECK(rig.memory().prepareProgramWrites(2, { { 100, {} } }, "x", rig.backupFolder, kNow, rig.now)
          == MemoryRefusal::ProgramOutOfRange);
    CHECK(rig.memory().prepareProgramWrites(5, writes, "x", rig.backupFolder, kNow, rig.now) == MemoryRefusal::NotARamBank);

    REQUIRE(rig.memory().prepareProgramWrites(2, writes, "my picks", rig.backupFolder, kNow, rig.now)
            == MemoryRefusal::None);
    rig.settle();
    REQUIRE(rig.status().plan);
    // Too many to list in the question.
    CHECK(rig.status().plan->question == "Overwrite 12 Programs in Bank 2 on the synth?");
    CHECK(rig.status().plan->programs == std::vector<int> { 2, 7, 11, 13, 17, 19, 23, 29, 31, 37, 40, 90 });
    REQUIRE(rig.confirm(NoBackupConsent::NotGiven) == MemoryConfirmResult::Accepted);
    rig.settle();

    CHECK(rig.status().stage == MemoryStage::Finished);
    CHECK(rig.status().message.find("Read back: 12 of 12") != std::string::npos);
    std::vector<int> specs;
    for (const auto& dump : rig.synth.receivedDumps())
        specs.push_back(dump.spec);
    CHECK(specs == std::vector<int> { 1, 6, 10, 12, 16, 18, 22, 28, 30, 36, 39, 89 });
    for (int location = 0; location < 99; ++location) {
        const bool written = std::any_of(writes.begin(), writes.end(),
                                         [location](const auto& write) { return write.program == location + 1; });
        CHECK(rig.synth.bankProgram(2, location)
              == (written ? patternProgram(6000 + location + 1) : patternProgram(200 + location)));
    }
}

TEST_CASE("Memory writer: reading several Banks writes no file and nothing to the synth")
{
    Rig rig(kVerified, "read-banks");
    CHECK(rig.memory().readBanks({}, rig.now) == MemoryRefusal::NothingToWrite);
    CHECK(rig.memory().readBanks({ 0, 4 }, rig.now) == MemoryRefusal::NotARamBank);

    REQUIRE(rig.memory().readBanks({ 1, 3 }, rig.now) == MemoryRefusal::None);
    CHECK(rig.status().total == 198);
    rig.settle();
    CHECK(rig.status().stage == MemoryStage::Finished);
    CHECK(rig.status().message == "Read Programs 1–99 of Banks 1 and 3 from the synth.");
    CHECK(rig.status().done == 198);
    CHECK_FALSE(rig.status().bankFile);
    CHECK_FALSE(std::filesystem::exists(rig.folder));
    CHECK(std::count_if(rig.toSynth.messages().begin(), rig.toSynth.messages().end(), isBankRequest) == 198);
    CHECK(rig.synth.receivedDumpCount(SimulatedNordLead2X::DumpDestination::Bank) == 0);
}
