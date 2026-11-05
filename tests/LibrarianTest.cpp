// The Librarian (#44): the synth's RAM Banks read and kept, Composed Banks, sending only the
// pending locations, storing one Library Program, and names from the Library metadata. Driven
// through the simulated synth; tests assert on what crosses the MIDI boundary, on files written
// and on what the Librarian shows the GUI.

#include "MemoryRig.h"

#include "nl2x/Librarian.h"
#include "nl2x/LibraryMetadata.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace nl2x;
using namespace nl2x::test;

namespace {

using SimProgram = SimulatedNordLead2X::Program;
using DumpDestination = SimulatedNordLead2X::DumpDestination;

Program asProgram(const SimProgram& values)
{
    return Program(values);
}

// A Library file holding one Program, as "Save Slot as .syx" writes it.
LibraryEntry programEntry(const std::string& name, int seed)
{
    const auto bytes = programEditBufferDump(0, Slot::A, patternProgram(seed));
    auto file = recogniseLibraryFile(name, bytes);
    REQUIRE(file.entries.size() == 1);
    return file.entries.front();
}

LibraryEntry kitEntry()
{
    std::vector<std::int8_t> kit(kPercussionKitParameterCount, 5);
    auto file = recogniseLibraryFile("drums", buildSysEx(0, 0, 16, kit));
    REQUIRE(file.entries.size() == 1);
    return file.entries.front();
}

// A Program Bank file of `programs` Programs (seeds 7000…) with header bytes of another tool.
LibraryFile bankFile(const std::string& name, int programs)
{
    Bytes bytes;
    for (int i = 0; i < programs; ++i) {
        const auto dump = bankProgramDump(3, 9, static_cast<std::uint8_t>((i + 11) % 99), patternProgram(7000 + i));
        bytes.insert(bytes.end(), dump.begin(), dump.end());
    }
    return recogniseLibraryFile(name, bytes);
}

struct LibrarianRig : Rig {
    explicit LibrarianRig(const std::string& testName, const hardware::Switches& switches = kVerified)
        : Rig(switches, testName, "librarian")
    {
    }

    Librarian& librarian() { return engine.librarian(); }

    void readSynthBanks()
    {
        REQUIRE(librarian().readSynthBanks(now) == MemoryRefusal::None);
        settle();
        REQUIRE(librarian().synthBanksStatus().stage == SynthBanksStage::Read);
    }

    int bankDumpsReceived() const { return synth.receivedDumpCount(DumpDestination::Bank); }
    int bankRequestsSent() const
    {
        return static_cast<int>(std::count_if(toSynth.messages().begin(), toSynth.messages().end(), isBankRequest));
    }
};

} // namespace

TEST_CASE("Librarian: reading the synth's Banks reads Programs 1–99 of Banks 0–3 and writes nothing")
{
    LibrarianRig rig("read");
    CHECK(rig.librarian().synthBanksStatus().stage == SynthBanksStage::NotRead);
    CHECK_FALSE(rig.librarian().synthProgram({ 0, 1 }));

    REQUIRE(rig.librarian().readSynthBanks(rig.now) == MemoryRefusal::None);
    CHECK(rig.librarian().synthBanksStatus().stage == SynthBanksStage::Reading);
    CHECK(rig.librarian().synthBanksStatus().total == 396);
    rig.runWhile([&] { return rig.librarian().synthBanksStatus().done < 150; });
    // Progress, and the Programs read so far, are visible while reading.
    CHECK(rig.librarian().synthBanksStatus().stage == SynthBanksStage::Reading);
    CHECK(rig.librarian().synthBankRead(0));
    CHECK_FALSE(rig.librarian().synthBankRead(1));
    CHECK(rig.librarian().synthProgram({ 1, 40 }) == asProgram(patternProgram(139)));

    rig.settle();
    CHECK(rig.librarian().synthBanksStatus().stage == SynthBanksStage::Read);
    CHECK(rig.bankRequestsSent() == 396);
    for (int bank = 0; bank < 4; ++bank) {
        CHECK(rig.librarian().synthBankRead(bank));
        for (int program = 1; program <= 99; ++program)
            CHECK(rig.librarian().synthProgram({ bank, program }) == asProgram(patternProgram(bank * 100 + program - 1)));
    }
    CHECK(rig.bankDumpsReceived() == 0);
    CHECK(std::none_of(rig.toSynth.messages().begin(), rig.toSynth.messages().end(), isBankDump));
    CHECK(rig.backups().empty());
    // Not composed yet: every location holds the synth's Program and nothing is pending.
    CHECK(rig.librarian().program({ 3, 99 }) == rig.librarian().synthProgram({ 3, 99 }));
    CHECK(rig.librarian().pendingPrograms(3).empty());
}

TEST_CASE("Librarian: Read again picks up what changed on the synth")
{
    LibrarianRig rig("read-again");
    rig.readSynthBanks();
    rig.synth.setBankProgram(1, 9, patternProgram(9999));

    rig.readSynthBanks();
    CHECK(rig.librarian().synthProgram({ 1, 10 }) == asProgram(patternProgram(9999)));
    CHECK(rig.bankRequestsSent() == 2 * 396);
    CHECK(rig.bankDumpsReceived() == 0);
}

TEST_CASE("Librarian: a read that stops part way keeps what was read and says why")
{
    LibrarianRig rig("read-fails");
    int requests = 0;
    rig.onSend = [&](std::span<const std::uint8_t> message) {
        if (isBankRequest(message) && ++requests == 150)
            rig.synth.setAnswersSysEx(false);
    };

    REQUIRE(rig.librarian().readSynthBanks(rig.now) == MemoryRefusal::None);
    rig.settle();

    const auto status = rig.librarian().synthBanksStatus();
    CHECK(status.stage == SynthBanksStage::Failed);
    CHECK(status.message.find("Program 51 of Bank 1 did not answer") != std::string::npos);
    CHECK(rig.librarian().synthBankRead(0));
    CHECK_FALSE(rig.librarian().synthBankRead(1));
    CHECK(rig.librarian().synthProgram({ 1, 50 }) == asProgram(patternProgram(149)));
    CHECK_FALSE(rig.librarian().synthProgram({ 1, 51 }));
    CHECK(rig.bankDumpsReceived() == 0);
}

TEST_CASE("Librarian: cancelling a read stops it")
{
    LibrarianRig rig("read-cancelled");
    REQUIRE(rig.librarian().readSynthBanks(rig.now) == MemoryRefusal::None);
    rig.runWhile([&] { return rig.librarian().synthBanksStatus().done < 20; });
    rig.memory().cancel();
    const auto requestsAtCancel = rig.bankRequestsSent();
    rig.runFor(3000);

    CHECK(rig.librarian().synthBanksStatus().stage == SynthBanksStage::Failed);
    CHECK(rig.bankRequestsSent() == requestsAtCancel);
    CHECK_FALSE(rig.librarian().synthBankRead(0));
}

TEST_CASE("Librarian: refusals")
{
    MidiCable nowhere;
    Engine unconnected(nowhere, kVerified);
    CHECK(unconnected.librarian().readSynthBanks(0) == MemoryRefusal::NotConnected);
    // A Composed Bank never starts empty.
    CHECK(unconnected.librarian().put({ 0, 1 }, programEntry("lead", 1)) == LibrarianRefusal::BankNotRead);
    CHECK(nowhere.messages().empty());

    LibrarianRig rig("refusals");
    rig.readSynthBanks();
    auto& librarian = rig.librarian();
    CHECK(librarian.put({ 4, 1 }, programEntry("lead", 1)) == LibrarianRefusal::NotARamBank);
    CHECK(librarian.put({ 0, 0 }, programEntry("lead", 1)) == LibrarianRefusal::ProgramOutOfRange);
    CHECK(librarian.put({ 0, 100 }, programEntry("lead", 1)) == LibrarianRefusal::ProgramOutOfRange);
    CHECK(librarian.put({ 0, 1 }, kitEntry()) == LibrarianRefusal::NotAProgram);
    CHECK(librarian.replaceWithFile(0, openLibraryFile(rig.folder / "missing.syx")) == LibrarianRefusal::NotAProgramBank);
    CHECK(librarian.replaceWithFile(0, bankFile("huge", 100)) == LibrarianRefusal::TooManyProgramsInFile);
    CHECK(librarian.prepareSend(0, rig.backupFolder, kNow, rig.now) == MemoryRefusal::NothingToWrite);
    CHECK(librarian.prepareStore(kitEntry(), { 0, 1 }, rig.backupFolder, kNow, rig.now) == MemoryRefusal::NotAProgram);
    CHECK(librarian.pendingPrograms(0).empty());
    CHECK(rig.bankDumpsReceived() == 0);
}

TEST_CASE("Librarian: composing a Bank marks the changed locations, which undo puts back")
{
    LibrarianRig rig("compose");
    rig.readSynthBanks();
    auto& librarian = rig.librarian();

    REQUIRE(librarian.put({ 2, 17 }, programEntry("lead", 1)) == LibrarianRefusal::None);
    REQUIRE(librarian.copy({ 0, 5 }, { 2, 3 }) == LibrarianRefusal::None);
    REQUIRE(librarian.copy({ 2, 17 }, { 2, 45 }) == LibrarianRefusal::None);

    CHECK(librarian.pendingPrograms(2) == std::vector<int> { 3, 17, 45 });
    CHECK(librarian.isPending({ 2, 17 }));
    CHECK_FALSE(librarian.isPending({ 2, 18 }));
    CHECK(librarian.program({ 2, 17 }) == asProgram(patternProgram(1)));
    CHECK(librarian.program({ 2, 3 }) == asProgram(patternProgram(4)));
    CHECK(librarian.program({ 2, 45 }) == asProgram(patternProgram(1)));
    // The synth's copy is what the synth holds: composing sends nothing.
    CHECK(librarian.synthProgram({ 2, 17 }) == asProgram(patternProgram(216)));
    CHECK(librarian.pendingPrograms(1).empty());
    CHECK_FALSE(librarian.wholeBankSource(2));

    librarian.undo({ 2, 17 });
    CHECK(librarian.pendingPrograms(2) == std::vector<int> { 3, 45 });
    CHECK(librarian.program({ 2, 17 }) == asProgram(patternProgram(216)));

    librarian.discardChanges(2);
    CHECK(librarian.pendingPrograms(2).empty());
    CHECK(rig.bankDumpsReceived() == 0);
}

TEST_CASE("Librarian: a Bank file replaces the whole Composed Bank, even before the synth is read")
{
    LibrarianRig rig("replace");
    auto& librarian = rig.librarian();

    REQUIRE(librarian.replaceWithFile(1, bankFile("my bank", 99)) == LibrarianRefusal::None);
    CHECK(librarian.wholeBankSource(1) == "my bank");
    CHECK(librarian.pendingPrograms(1).size() == 99);
    // By position in the file, never by its spec bytes.
    CHECK(librarian.program({ 1, 1 }) == asProgram(patternProgram(7000)));
    CHECK(librarian.program({ 1, 99 }) == asProgram(patternProgram(7098)));
    // A Bank file is a starting point: locations can be changed on top of it.
    CHECK(librarian.put({ 1, 2 }, programEntry("lead", 1)) == LibrarianRefusal::None);
    CHECK(librarian.program({ 1, 2 }) == asProgram(patternProgram(1)));

    // A shorter file leaves the other locations as the synth has them.
    REQUIRE(librarian.replaceWithFile(0, bankFile("short", 10)) == LibrarianRefusal::None);
    CHECK(librarian.pendingPrograms(0).size() == 10);
    CHECK_FALSE(librarian.program({ 0, 11 }));

    librarian.discardChanges(1);
    CHECK_FALSE(librarian.wholeBankSource(1));
}

TEST_CASE("Librarian: a Composed Bank saved as a file is a Program Bank the Library reads back")
{
    LibrarianRig rig("save");
    rig.readSynthBanks();
    auto& librarian = rig.librarian();
    REQUIRE(librarian.put({ 2, 17 }, programEntry("lead", 1)) == LibrarianRefusal::None);
    const auto path = rig.folder / "Composed.syx";

    REQUIRE(librarian.saveBankFile(2, path) == LibrarianRefusal::None);
    const auto file = openLibraryFile(path);
    CHECK(file.kind == LibraryFileKind::ProgramBank);
    CHECK(file.ignoredMessageCount == 0);
    REQUIRE(file.entries.size() == 99);
    for (int program = 1; program <= 99; ++program) {
        const auto& dump = std::get<ProgramDump>(file.entries[static_cast<std::size_t>(program - 1)].dump);
        CHECK(dump.program == *librarian.program({ 2, program }));
        // Typed as the synth's own replies for Bank 2 (SysEx Bank 3), as "Read Bank into file" keeps them.
        CHECK(dump.type == 3);
        CHECK(dump.spec == program - 1);
        CHECK(dump.globalChannel == *rig.engine.globalChannel());
    }
    CHECK(file.entries[16].name == "Composed – 17");

    CHECK(librarian.saveBankFile(2, path) == LibrarianRefusal::FileExists);
    CHECK(librarian.saveBankFile(2, path, ReplaceExisting::Yes) == LibrarianRefusal::None);
    // Saving sends nothing and keeps the changes pending.
    CHECK(librarian.pendingPrograms(2) == std::vector<int> { 17 });
    CHECK(rig.bankDumpsReceived() == 0);
}

TEST_CASE("Librarian: a Bank with unknown locations before known ones cannot be saved")
{
    LibrarianRig rig("save-gap");
    auto& librarian = rig.librarian();
    REQUIRE(librarian.replaceWithFile(0, bankFile("short", 10)) == LibrarianRefusal::None);
    REQUIRE(librarian.saveBankFile(0, rig.folder / "Short.syx") == LibrarianRefusal::None);
    CHECK(openLibraryFile(rig.folder / "Short.syx").entries.size() == 10);

    REQUIRE(librarian.put({ 0, 50 }, programEntry("lead", 1)) == LibrarianRefusal::None);
    CHECK(librarian.saveBankFile(0, rig.folder / "Gap.syx") == LibrarianRefusal::BankIncomplete);
    CHECK_FALSE(std::filesystem::exists(rig.folder / "Gap.syx"));
}

TEST_CASE("Librarian: Send backs up the Bank, asks, then writes only the pending locations")
{
    LibrarianRig rig("send");
    rig.readSynthBanks();
    auto& librarian = rig.librarian();
    REQUIRE(librarian.put({ 2, 17 }, programEntry("lead", 1)) == LibrarianRefusal::None);
    REQUIRE(librarian.put({ 2, 3 }, programEntry("bass", 2)) == LibrarianRefusal::None);
    REQUIRE(librarian.copy({ 0, 1 }, { 2, 45 }) == LibrarianRefusal::None);
    const auto requestsBefore = rig.bankRequestsSent();

    REQUIRE(librarian.prepareSend(2, rig.backupFolder, kNow, rig.now) == MemoryRefusal::None);
    rig.settle();

    // 1. Backup of the whole Bank, re-read from the synth, before the question.
    REQUIRE(rig.status().stage == MemoryStage::AwaitingConfirmation);
    CHECK(rig.bankRequestsSent() - requestsBefore == 99);
    const auto plan = *rig.status().plan;
    REQUIRE(plan.backupFile);
    CHECK(openLibraryFile(*plan.backupFile).entries.size() == 99);
    CHECK_FALSE(plan.needsNoBackupConsent);
    CHECK(plan.operation == MemoryOperation::SendPrograms);
    CHECK(plan.programs == std::vector<int> { 3, 17, 45 });
    CHECK(plan.question == "Overwrite Programs 3, 17 and 45 in Bank 2 on the synth?");
    CHECK(plan.details.find("The other Programs of Bank 2 are not touched") != std::string::npos);
    CHECK(rig.bankDumpsReceived() == 0);

    // 2. Confirmed: one single Program dump per pending location.
    REQUIRE(rig.confirm(NoBackupConsent::NotGiven) == MemoryConfirmResult::Accepted);
    rig.settle();
    CHECK(rig.status().stage == MemoryStage::Finished);
    CHECK(rig.status().message.find("Read back: 3 of 3") != std::string::npos);
    REQUIRE(rig.bankDumpsReceived() == 3);
    std::vector<int> specs;
    for (const auto& dump : rig.synth.receivedDumps())
        if (dump.destination == DumpDestination::Bank) {
            specs.push_back(dump.spec);
            CHECK(dump.type == 3);
        }
    CHECK(specs == std::vector<int> { 2, 16, 44 });
    CHECK(std::all_of(rig.backupExistedAtBankDump.begin(), rig.backupExistedAtBankDump.end(), [](bool b) { return b; }));
    for (std::size_t i = 1; i < rig.bankDumpTimes.size(); ++i)
        CHECK(rig.bankDumpTimes[i] - rig.bankDumpTimes[i - 1] >= hardware::kBankWriteSpacingMs);

    // 3. The synth holds exactly the composition; the Librarian's copy shows it, nothing is pending.
    for (int program = 1; program <= 99; ++program) {
        const auto expected = program == 3 ? patternProgram(2) : program == 17 ? patternProgram(1)
                              : program == 45                  ? patternProgram(0)
                                                               : patternProgram(200 + program - 1);
        CHECK(rig.synth.bankProgram(2, program - 1) == expected);
        CHECK(librarian.synthProgram({ 2, program }) == asProgram(expected));
    }
    CHECK(librarian.pendingPrograms(2).empty());
    CHECK(rig.synth.bankProgram(1, 16) == patternProgram(116));
}

TEST_CASE("Librarian: Send that cannot back up the Bank writes nothing and keeps the changes")
{
    LibrarianRig rig("send-backup-fails");
    rig.readSynthBanks();
    REQUIRE(rig.librarian().put({ 2, 17 }, programEntry("lead", 1)) == LibrarianRefusal::None);
    int requests = 0;
    rig.onSend = [&](std::span<const std::uint8_t> message) {
        if (isBankRequest(message) && ++requests == 30)
            rig.synth.setAnswersSysEx(false);
    };

    REQUIRE(rig.librarian().prepareSend(2, rig.backupFolder, kNow, rig.now) == MemoryRefusal::None);
    rig.settle();
    CHECK(rig.status().stage == MemoryStage::Failed);
    CHECK(rig.backups().empty());
    rig.synth.setAnswersSysEx(true);
    rig.runFor(3000);
    CHECK(rig.bankDumpsReceived() == 0);
    CHECK(rig.librarian().pendingPrograms(2) == std::vector<int> { 17 });
}

TEST_CASE("Librarian: Send cancelled at the confirmation writes nothing and keeps the changes")
{
    LibrarianRig rig("send-cancelled");
    rig.readSynthBanks();
    REQUIRE(rig.librarian().put({ 2, 17 }, programEntry("lead", 1)) == LibrarianRefusal::None);
    REQUIRE(rig.librarian().prepareSend(2, rig.backupFolder, kNow, rig.now) == MemoryRefusal::None);
    rig.settle();
    REQUIRE(rig.status().plan);
    const auto token = rig.status().plan->token;

    rig.memory().cancel();
    CHECK(rig.memory().confirm(token, NoBackupConsent::Given, rig.now) == MemoryConfirmResult::RefusedNoSuchPlan);
    rig.runFor(3000);
    CHECK(rig.bankDumpsReceived() == 0);
    CHECK(rig.librarian().pendingPrograms(2) == std::vector<int> { 17 });
    // The backup was made all the same, and the synth's copy came from it.
    CHECK(rig.backups().size() == 1);
}

TEST_CASE("Librarian: a Send that went to another Bank leaves its locations pending")
{
    LibrarianRig rig("send-wrong-bank");
    rig.readSynthBanks();
    rig.synth.selectBank(1);
    REQUIRE(rig.librarian().put({ 2, 17 }, programEntry("lead", 1)) == LibrarianRefusal::None);
    REQUIRE(rig.librarian().prepareSend(2, rig.backupFolder, kNow, rig.now) == MemoryRefusal::None);
    rig.settle();
    REQUIRE(rig.confirm(NoBackupConsent::NotGiven) == MemoryConfirmResult::Accepted);
    rig.settle();

    REQUIRE(rig.status().stage == MemoryStage::Failed);
    CHECK(rig.status().message.find("Not stored: Program 17 in Bank 2") != std::string::npos);
    CHECK(rig.synth.bankProgram(1, 16) == patternProgram(1));
    CHECK(rig.librarian().pendingPrograms(2) == std::vector<int> { 17 });
    CHECK(rig.librarian().synthProgram({ 2, 17 }) == asProgram(patternProgram(216)));

    rig.synth.selectBank(2);
    REQUIRE(rig.librarian().prepareSend(2, rig.backupFolder, kNow, rig.now) == MemoryRefusal::None);
    rig.settle();
    REQUIRE(rig.confirm(NoBackupConsent::NotGiven) == MemoryConfirmResult::Accepted);
    rig.settle();
    CHECK(rig.status().stage == MemoryStage::Finished);
    CHECK(rig.synth.bankProgram(2, 16) == patternProgram(1));
    CHECK(rig.librarian().pendingPrograms(2).empty());
}

TEST_CASE("Librarian: a Send stopped part way leaves only the locations not written pending")
{
    LibrarianRig rig("send-interrupted");
    rig.readSynthBanks();
    auto& librarian = rig.librarian();
    for (int program = 10; program < 20; ++program)
        REQUIRE(librarian.put({ 2, program }, programEntry("sound", 3000 + program)) == LibrarianRefusal::None);
    REQUIRE(librarian.prepareSend(2, rig.backupFolder, kNow, rig.now) == MemoryRefusal::None);
    rig.settle();
    CHECK(rig.status().plan->question == "Overwrite Programs 10–19 in Bank 2 on the synth?");
    REQUIRE(rig.confirm(NoBackupConsent::NotGiven) == MemoryConfirmResult::Accepted);
    rig.runWhile([&] { return rig.bankDumpTimes.size() < 4; });

    // The user turns the dial on the synth: the write stops.
    rig.synth.selectProgramOnPanel(Slot::A, 1, 0);
    rig.runFor(5000);

    CHECK(rig.status().stage == MemoryStage::Failed);
    CHECK(rig.bankDumpsReceived() == 4);
    CHECK(librarian.pendingPrograms(2) == std::vector<int> { 14, 15, 16, 17, 18, 19 });
    for (int program = 10; program < 14; ++program)
        CHECK(librarian.synthProgram({ 2, program }) == asProgram(patternProgram(3000 + program)));
    CHECK(librarian.synthProgram({ 2, 14 }) == asProgram(patternProgram(213)));
    CHECK(librarian.program({ 2, 14 }) == asProgram(patternProgram(3014)));
}

TEST_CASE("Librarian: a Bank replaced from a file is sent whole")
{
    LibrarianRig rig("send-whole");
    auto& librarian = rig.librarian();
    rig.synth.selectBank(1);
    REQUIRE(librarian.replaceWithFile(1, bankFile("my bank", 99)) == LibrarianRefusal::None);

    REQUIRE(librarian.prepareSend(1, rig.backupFolder, kNow, rig.now) == MemoryRefusal::None);
    rig.settle();
    CHECK(rig.status().plan->question == "Overwrite Programs 1–99 in Bank 1 on the synth?");
    CHECK(rig.status().plan->details.find("\"my bank\"") != std::string::npos);
    REQUIRE(rig.confirm(NoBackupConsent::NotGiven) == MemoryConfirmResult::Accepted);
    rig.settle();

    CHECK(rig.status().stage == MemoryStage::Finished);
    CHECK(rig.bankDumpsReceived() == 99);
    for (int location = 0; location < 99; ++location)
        CHECK(rig.synth.bankProgram(1, location) == patternProgram(7000 + location));
    CHECK(librarian.pendingPrograms(1).empty());
    CHECK_FALSE(librarian.wholeBankSource(1));
    // The backup read taught the Librarian the rest of the synth's Bank 1 before it was overwritten.
    CHECK(librarian.synthBankRead(1));
    CHECK(librarian.synthProgram({ 1, 50 }) == asProgram(patternProgram(7049)));
}

TEST_CASE("Librarian, locked: Send needs the no-backup consent")
{
    LibrarianRig rig("send-locked", kNotVerified);
    REQUIRE(rig.librarian().replaceWithFile(2, bankFile("my bank", 5)) == LibrarianRefusal::None);
    REQUIRE(rig.librarian().prepareSend(2, rig.backupFolder, kNow, rig.now) == MemoryRefusal::None);
    rig.settle();
    REQUIRE(rig.status().plan);
    CHECK(rig.status().plan->needsNoBackupConsent);
    CHECK(rig.confirm(NoBackupConsent::NotGiven) == MemoryConfirmResult::RefusedNoBackupConsent);
    rig.runFor(2000);
    CHECK(rig.bankDumpsReceived() == 0);

    REQUIRE(rig.confirm(NoBackupConsent::Given) == MemoryConfirmResult::Accepted);
    rig.settle();
    CHECK(rig.bankDumpsReceived() == 5);
    CHECK(rig.librarian().pendingPrograms(2).empty());
    CHECK(rig.librarian().synthProgram({ 2, 5 }) == asProgram(patternProgram(7004)));
}

TEST_CASE("Librarian: storing a single Library Program backs up its Bank and writes one location")
{
    LibrarianRig rig("store");
    rig.readSynthBanks();
    const auto entry = programEntry("Fat bass", 42);

    REQUIRE(rig.librarian().prepareStore(entry, { 2, 5 }, rig.backupFolder, kNow, rig.now) == MemoryRefusal::None);
    rig.settle();
    REQUIRE(rig.status().plan);
    CHECK(rig.status().plan->operation == MemoryOperation::StoreProgram);
    CHECK(rig.status().plan->question == "Overwrite Program 5 in Bank 2 on the synth?");
    CHECK(rig.status().plan->details.find("\"Fat bass\" replaces Program 5 in Bank 2") != std::string::npos);
    REQUIRE(rig.status().plan->backupFile);

    REQUIRE(rig.confirm(NoBackupConsent::NotGiven) == MemoryConfirmResult::Accepted);
    rig.settle();
    CHECK(rig.status().stage == MemoryStage::Finished);
    CHECK(rig.status().message.find("Stored \"Fat bass\" as Program 5 in Bank 2") != std::string::npos);
    CHECK(rig.bankDumpsReceived() == 1);
    CHECK(rig.backupExistedAtBankDump == std::vector<bool> { true });
    CHECK(rig.synth.bankProgram(2, 4) == patternProgram(42));
    CHECK(rig.synth.bankProgram(2, 5) == patternProgram(205));
    CHECK(rig.librarian().synthProgram({ 2, 5 }) == asProgram(patternProgram(42)));
    CHECK(rig.librarian().pendingPrograms(2).empty());
}

TEST_CASE("Librarian: a single store cancelled at the confirmation writes nothing")
{
    LibrarianRig rig("store-cancelled");
    REQUIRE(rig.librarian().prepareStore(programEntry("lead", 1), { 2, 5 }, rig.backupFolder, kNow, rig.now)
            == MemoryRefusal::None);
    rig.settle();
    REQUIRE(rig.status().stage == MemoryStage::AwaitingConfirmation);
    rig.memory().cancel();
    rig.runFor(2000);
    CHECK(rig.bankDumpsReceived() == 0);
    CHECK(rig.synth.bankProgram(2, 4) == patternProgram(204));
    // The backup read is kept as the Librarian's copy of Bank 2.
    CHECK(rig.librarian().synthBankRead(2));
}

TEST_CASE("Librarian: names and tags of a location come from the Library metadata by fingerprint")
{
    LibrarianRig rig("metadata");
    rig.readSynthBanks();
    auto metadata = LibraryMetadata::load(rig.folder / "Library");
    const auto stored = *rig.librarian().synthProgram({ 1, 7 });
    REQUIRE(metadata.set(SoundFingerprint::of(stored), { "Glass pad", { "Pad" }, true }) == MetadataSaveResult::Saved);

    // Exact match, on the synth and in another location where the same Program was put.
    CHECK(rig.librarian().soundInfo(metadata, { 1, 7 }).name == "Glass pad");
    CHECK(rig.librarian().soundMatch(metadata, { 1, 7 })->differences == 0);
    REQUIRE(rig.librarian().copy({ 1, 7 }, { 3, 1 }) == LibrarianRefusal::None);
    CHECK(rig.librarian().soundInfo(metadata, { 3, 1 }).hasTag("Pad"));

    // The same Program edited on the panel (two values) is still that sound.
    auto edited = stored;
    edited.setValue(ProgramParameter::FilterCutoff, edited.value(ProgramParameter::FilterCutoff) + 1);
    edited.setValue(ProgramParameter::FilterResonance, edited.value(ProgramParameter::FilterResonance) + 1);
    REQUIRE(rig.librarian().put({ 3, 2 }, edited) == LibrarianRefusal::None);
    const auto similar = rig.librarian().soundMatch(metadata, { 3, 2 });
    REQUIRE(similar);
    CHECK(similar->differences == 2);
    CHECK(rig.librarian().soundInfo(metadata, { 3, 2 }).favourite);

    // Other Programs, and unknown locations, have no information.
    CHECK(rig.librarian().soundInfo(metadata, { 1, 8 }).isEmpty());
    CHECK_FALSE(rig.librarian().soundMatch(metadata, { 1, 8 }));
    CHECK(rig.librarian().soundInfo(metadata, { 5, 1 }).isEmpty());
}
