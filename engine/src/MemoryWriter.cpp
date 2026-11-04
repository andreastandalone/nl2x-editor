#include "nl2x/MemoryWriter.h"

#include "SynthConnection.h"

#include "nl2x/Engine.h"
#include "nl2x/Performance.h"
#include "nl2x/SysEx.h"

#include <algorithm>
#include <cstdio>
#include <numeric>
#include <utility>
#include <variant>

namespace nl2x {

namespace {

constexpr SynthConnection::Owner kMemoryWriterOwner = 2;
constexpr int kProgramsPerBank = hardware::kProgramsPerRamBank;

// A reply to a Bank location request: a Program dump, matched by size and never by header
// (research §3), whose data decodes without loss.
bool isProgramDumpReply(std::span<const std::uint8_t> message)
{
    return message.size() == kProgramDumpSize && parseProgramDump(message).has_value();
}

bool isPerformanceReply(std::span<const std::uint8_t> message)
{
    return message.size() == kPerformanceDumpSize && parsePerformanceDump(message).has_value();
}

std::string bankText(int bank)
{
    return "Bank " + std::to_string(bank);
}

std::string twoDigits(int value)
{
    char text[8];
    std::snprintf(text, sizeof text, "%02d", value);
    return text;
}

// "Bank 2 backup 2026-10-28 14.03.07": sorts by date, and uses no character macOS refuses.
std::string backupName(int bank, const WallClockTime& time)
{
    return bankText(bank) + " backup " + std::to_string(time.year) + "-" + twoDigits(time.month) + "-"
           + twoDigits(time.day) + " " + twoDigits(time.hour) + "." + twoDigits(time.minute) + "."
           + twoDigits(time.second);
}

std::vector<std::uint8_t> joined(const std::vector<std::vector<std::uint8_t>>& messages)
{
    std::vector<std::uint8_t> bytes;
    for (const auto& message : messages)
        bytes.insert(bytes.end(), message.begin(), message.end());
    return bytes;
}

bool isRamBank(int bank)
{
    return bank >= hardware::kFirstRamProgramBank && bank <= hardware::kLastRamProgramBank;
}

std::vector<int> firstLocations(int count)
{
    std::vector<int> locations(static_cast<std::size_t>(count));
    std::iota(locations.begin(), locations.end(), 0);
    return locations;
}

std::string inQuotes(const std::string& name)
{
    return "\"" + name + "\"";
}

// "0, 1 and 3"
std::string listed(const std::vector<int>& numbers)
{
    std::string list;
    for (std::size_t i = 0; i < numbers.size(); ++i)
        list += (i == 0 ? "" : i + 1 == numbers.size() ? " and " : ", ") + std::to_string(numbers[i]);
    return list;
}

} // namespace

std::string memoryRefusalText(MemoryRefusal refusal)
{
    switch (refusal) {
    case MemoryRefusal::None:
        return {};
    case MemoryRefusal::NotConnected:
        return "Not connected to the synth: nothing was done.";
    case MemoryRefusal::RequestsSuspended:
        return "The Probe is talking to the synth: try again when its script has finished.";
    case MemoryRefusal::Busy:
        return "Another read or write of the synth's memory is under way: finish or cancel it first.";
    case MemoryRefusal::NotARamBank:
        return "Only Banks 0–3 can be written (Banks 4–9 are ROM).";
    case MemoryRefusal::ProgramOutOfRange:
        return "A Bank holds Programs 1–99.";
    case MemoryRefusal::SlotHasNoProgram:
        return "The Slot has no Program to store: it has not been read from the synth, or it is playing a "
               "Percussion Kit.";
    case MemoryRefusal::NotAProgramBank:
        return "Choose a Program Bank file (a file with several Programs) in the Library.";
    case MemoryRefusal::NoProgramsInFile:
        return "The file holds no Programs.";
    case MemoryRefusal::TooManyProgramsInFile:
        return "The file holds more than 99 Programs, more than a Bank.";
    case MemoryRefusal::RequestTypeCollides:
        return "This Bank cannot be read yet: the assumed request type for it is the Slot Edit Buffer request "
               "(hardware discovery step 5 finds the right one).";
    case MemoryRefusal::FileExists:
        return "The file already exists and was not replaced.";
    case MemoryRefusal::NothingToWrite:
        return "Nothing to send: no Program location has been changed.";
    case MemoryRefusal::DuplicateProgram:
        return "A Program location is named twice.";
    case MemoryRefusal::NotAProgram:
        return "Only Programs can be stored in a Program location (Percussion Kits and Performances are not "
               "supported yet).";
    }
    return {};
}

MemoryWriter::MemoryWriter(const Engine& owner, SynthConnection& synthConnection, const hardware::Switches& switches,
                           std::function<void()> changedCallback)
    : engine(owner),
      connection(synthConnection),
      reading { switches.bankReadingVerified, switches.bankRequestTypeOfSysExBank1 },
      performanceDumpsApplied(switches.performanceEditBufferDumpIsApplied),
      onChange(std::move(changedCallback))
{
}

bool MemoryWriter::busy() const
{
    switch (current.stage) {
    case MemoryStage::Idle:
    case MemoryStage::Finished:
    case MemoryStage::Failed:
        return false;
    default:
        return true;
    }
}

bool MemoryWriter::holdsEditBufferDumps() const
{
    return current.stage == MemoryStage::CheckingSelectedBank || current.stage == MemoryStage::Writing
           || current.stage == MemoryStage::Verifying;
}

MemoryRefusal MemoryWriter::startRefusal(int bank) const
{
    if (! engine.globalChannel())
        return MemoryRefusal::NotConnected;
    if (engine.requestsSuspended())
        return MemoryRefusal::RequestsSuspended;
    if (busy())
        return MemoryRefusal::Busy;
    if (! isRamBank(bank))
        return MemoryRefusal::NotARamBank;
    return MemoryRefusal::None;
}

std::uint8_t MemoryWriter::requestTypeOf(int bank) const
{
    return static_cast<std::uint8_t>(reading.requestTypeOfSysExBank1 + hardware::sysExBankOfShownBank(bank) - 1);
}

void MemoryWriter::begin(MemoryOperation operation, int bank, std::vector<int> locations)
{
    task = Task {};
    task->operation = operation;
    task->bank = bank;
    task->locations = std::move(locations);
    current = MemoryTaskStatus {};
    current.operation = operation;
    current.bank = bank;
}

MemoryRefusal MemoryWriter::prepareStoreProgram(Slot slot, int bank, int program,
                                                const std::filesystem::path& backupFolder, WallClockTime now,
                                                TimeMs timeMs)
{
    if (const auto refusal = startRefusal(bank); refusal != MemoryRefusal::None)
        return refusal;
    if (program < 1 || program > kProgramsPerBank)
        return MemoryRefusal::ProgramOutOfRange;
    if (! engine.slotState(slot).program)
        return MemoryRefusal::SlotHasNoProgram;

    return startWrite(MemoryOperation::StoreProgram, bank, { program - 1 }, {}, backupFolder, now, timeMs,
                      [slot](Task& newTask) { newTask.slot = slot; });
}

MemoryRefusal MemoryWriter::prepareStoreProgram(const Program& value, const std::string& sourceName, int bank,
                                                int program, const std::filesystem::path& backupFolder,
                                                WallClockTime now, TimeMs timeMs)
{
    if (const auto refusal = startRefusal(bank); refusal != MemoryRefusal::None)
        return refusal;
    if (program < 1 || program > kProgramsPerBank)
        return MemoryRefusal::ProgramOutOfRange;
    return startWrite(MemoryOperation::StoreProgram, bank, { program - 1 }, { value }, backupFolder, now, timeMs,
                      [&sourceName](Task& newTask) { newTask.sourceName = sourceName; });
}

MemoryRefusal MemoryWriter::prepareProgramWrites(int bank, std::vector<ProgramWrite> writes,
                                                 const std::string& sourceName,
                                                 const std::filesystem::path& backupFolder, WallClockTime now,
                                                 TimeMs timeMs)
{
    if (const auto refusal = startRefusal(bank); refusal != MemoryRefusal::None)
        return refusal;
    if (writes.empty())
        return MemoryRefusal::NothingToWrite;
    std::sort(writes.begin(), writes.end(), [](const auto& a, const auto& b) { return a.program < b.program; });
    std::vector<int> locations;
    std::vector<Program> programs;
    for (const auto& write : writes) {
        if (write.program < 1 || write.program > kProgramsPerBank)
            return MemoryRefusal::ProgramOutOfRange;
        if (! locations.empty() && locations.back() == write.program - 1)
            return MemoryRefusal::DuplicateProgram;
        locations.push_back(write.program - 1);
        programs.push_back(write.value);
    }
    return startWrite(MemoryOperation::SendPrograms, bank, std::move(locations), std::move(programs), backupFolder,
                      now, timeMs, [&sourceName](Task& newTask) { newTask.sourceName = sourceName; });
}

MemoryRefusal MemoryWriter::startWrite(MemoryOperation operation, int bank, std::vector<int> locations,
                                       std::vector<Program> programs, const std::filesystem::path& backupFolder,
                                       WallClockTime now, TimeMs timeMs, const std::function<void(Task&)>& setUp)
{
    if (reading.verified && requestTypeOf(bank) == hardware::kSlotEditBufferRequestType)
        return MemoryRefusal::RequestTypeCollides;
    begin(operation, bank, std::move(locations));
    task->programs = std::move(programs);
    task->backupFolder = backupFolder;
    task->time = now;
    setUp(*task);
    if (reading.verified) {
        current.stage = MemoryStage::ReadingBackup;
        current.total = kProgramsPerBank;
        changed();
        readLocation(0, timeMs);
    } else {
        offerPlan({});
    }
    return MemoryRefusal::None;
}

MemoryRefusal MemoryWriter::prepareBankSend(const LibraryFile& file, int bank,
                                            const std::filesystem::path& backupFolder, WallClockTime now,
                                            TimeMs timeMs)
{
    if (const auto refusal = startRefusal(bank); refusal != MemoryRefusal::None)
        return refusal;
    if (file.kind != LibraryFileKind::ProgramBank)
        return MemoryRefusal::NotAProgramBank;

    std::vector<Program> programs;
    std::size_t kits = 0;
    for (const auto& entry : file.entries) {
        if (const auto* dump = std::get_if<ProgramDump>(&entry.dump))
            programs.push_back(dump->program);
        else
            ++kits;
    }
    if (programs.empty())
        return MemoryRefusal::NoProgramsInFile;
    if (programs.size() > static_cast<std::size_t>(kProgramsPerBank))
        return MemoryRefusal::TooManyProgramsInFile;

    auto locations = firstLocations(static_cast<int>(programs.size()));
    return startWrite(MemoryOperation::SendBank, bank, std::move(locations), std::move(programs), backupFolder, now,
                      timeMs, [&file, kits](Task& newTask) {
                          newTask.sourceName = file.name;
                          newTask.skippedKits = kits;
                      });
}

MemoryRefusal MemoryWriter::readBank(int bank, const std::filesystem::path& file, ReplaceExisting replace,
                                     TimeMs timeMs)
{
    if (const auto refusal = startRefusal(bank); refusal != MemoryRefusal::None)
        return refusal;
    if (requestTypeOf(bank) == hardware::kSlotEditBufferRequestType)
        return MemoryRefusal::RequestTypeCollides;
    std::error_code error;
    if (replace == ReplaceExisting::No && std::filesystem::exists(file, error))
        return MemoryRefusal::FileExists;

    begin(MemoryOperation::ReadBank, bank, firstLocations(kProgramsPerBank));
    task->readFile = file;
    task->replace = replace;
    current.stage = MemoryStage::ReadingBank;
    current.total = kProgramsPerBank;
    changed();
    readLocation(0, timeMs);
    return MemoryRefusal::None;
}

MemoryRefusal MemoryWriter::readBanks(std::vector<int> banks, TimeMs timeMs)
{
    if (banks.empty())
        return MemoryRefusal::NothingToWrite;
    for (const auto bank : banks) {
        if (const auto refusal = startRefusal(bank); refusal != MemoryRefusal::None)
            return refusal;
        if (requestTypeOf(bank) == hardware::kSlotEditBufferRequestType)
            return MemoryRefusal::RequestTypeCollides;
    }

    begin(MemoryOperation::ReadBanks, banks.front(), firstLocations(kProgramsPerBank));
    current.stage = MemoryStage::ReadingBank;
    current.total = kProgramsPerBank * static_cast<int>(banks.size());
    task->banksToRead = std::move(banks);
    changed();
    readLocation(0, timeMs);
    return MemoryRefusal::None;
}

void MemoryWriter::readLocation(int location, TimeMs timeMs)
{
    const auto channel = engine.globalChannel();
    if (! channel) {
        fail("The connection to the synth was lost. Nothing was written to the synth.");
        return;
    }
    connection.enqueue(
        bankLocationRequest(*channel, requestTypeOf(task->bank), static_cast<std::uint8_t>(location)),
        isProgramDumpReply,
        [this, location](auto reply, TimeMs doneMs) { locationRead(location, reply, doneMs); }, timeMs,
        kMemoryWriterOwner);
}

void MemoryWriter::locationRead(int location, std::optional<std::span<const std::uint8_t>> reply, TimeMs timeMs)
{
    if (! task)
        return;
    const auto program = "Program " + std::to_string(location + 1) + " of " + bankText(task->bank);

    if (current.stage == MemoryStage::Verifying) {
        const auto index = static_cast<std::size_t>(current.done);
        if (! reply) {
            finish(locationsText() + " written, but " + program + " did not answer the read-back, so the write "
                   "could not be checked.");
            return;
        }
        const auto readBack = parseProgramDump(*reply)->program;
        if (readBack == task->programs[index])
            ++task->matches;
        if (listener)
            listener->programRead(task->bank, location + 1, readBack);
        ++current.done;
        changed();
        if (index + 1 < task->locations.size()) {
            readLocation(task->locations[index + 1], timeMs);
        } else {
            verified(timeMs);
        }
        return;
    }

    if (! reply) {
        const auto noAnswer = program + " did not answer (request type " + std::to_string(requestTypeOf(task->bank))
                              + ")";
        if (current.stage == MemoryStage::ReadingBackup)
            fail(noAnswer + ": no backup was made, and nothing was written to the synth.");
        else if (task->operation == MemoryOperation::ReadBanks)
            fail(noAnswer + ": the Programs read before it are kept.");
        else
            fail(noAnswer + ": no file was written.");
        return;
    }

    task->replies.emplace_back(reply->begin(), reply->end());
    current.done = static_cast<int>(task->bankIndex) * kProgramsPerBank + static_cast<int>(task->replies.size());
    if (listener)
        listener->programRead(task->bank, location + 1, parseProgramDump(*reply)->program);
    changed();
    if (location + 1 < kProgramsPerBank)
        readLocation(location + 1, timeMs);
    else if (current.stage == MemoryStage::ReadingBackup)
        backupComplete(timeMs);
    else if (task->operation == MemoryOperation::ReadBanks)
        nextBankRead(timeMs);
    else
        readComplete();
}

void MemoryWriter::nextBankRead(TimeMs timeMs)
{
    if (++task->bankIndex < task->banksToRead.size()) {
        task->bank = task->banksToRead[task->bankIndex];
        task->replies.clear();
        current.bank = task->bank;
        changed();
        readLocation(0, timeMs);
        return;
    }
    const auto banks = task->banksToRead;
    finish("Read Programs 1–99 of " + std::string(banks.size() == 1 ? "Bank " : "Banks ") + listed(banks)
           + " from the synth.");
}

void MemoryWriter::backupComplete(TimeMs)
{
    // Written now, before any confirmation is asked for, so it exists whatever happens next.
    const auto path = uniqueLibraryPath(task->backupFolder, backupName(task->bank, task->time));
    if (saveLibraryFile(path, joined(task->replies)) != LibrarySaveResult::Saved) {
        fail("Could not write the backup file " + path.string() + ". Nothing was written to the synth.");
        return;
    }
    current.backupFile = path;
    offerPlan({});
}

void MemoryWriter::readComplete()
{
    const auto result = saveLibraryFile(task->readFile, joined(task->replies), task->replace);
    if (result != LibrarySaveResult::Saved) {
        fail(result == LibrarySaveResult::AlreadyExists
                 ? task->readFile.string() + " already exists and was not replaced."
                 : "Could not write " + task->readFile.string() + ".");
        return;
    }
    current.bankFile = task->readFile;
    std::string message = "Read Programs 1–99 of " + bankText(task->bank) + " into " + task->readFile.string() + ".";
    if (! reading.verified)
        message += " Experimental: the Bank request type " + std::to_string(requestTypeOf(task->bank))
                   + " is not verified on hardware, so check the file before relying on it.";
    finish(message);
}

std::string MemoryWriter::locationsText() const
{
    const auto& locations = task->locations;
    const auto in = " in " + bankText(task->bank);
    const auto first = locations.front() + 1;
    if (locations.size() == 1)
        return "Program " + std::to_string(first) + in;
    const auto last = locations.back() + 1;
    if (last - first + 1 == static_cast<int>(locations.size()))
        return "Programs " + std::to_string(first) + "–" + std::to_string(last) + in;
    // Listed while the question stays readable.
    constexpr std::size_t kMostListed = 10;
    if (locations.size() > kMostListed)
        return std::to_string(locations.size()) + " Programs" + in;
    std::vector<int> programs;
    for (const auto location : locations)
        programs.push_back(location + 1);
    return "Programs " + listed(programs) + in;
}

void MemoryWriter::offerPlan(std::string message)
{
    MemoryWritePlan plan;
    plan.token = ++lastToken;
    plan.operation = task->operation;
    plan.bank = task->bank;
    plan.firstProgram = task->locations.front() + 1;
    plan.programCount = static_cast<int>(task->locations.size());
    for (const auto location : task->locations)
        plan.programs.push_back(location + 1);
    plan.backupFile = current.backupFile;
    plan.needsNoBackupConsent = ! current.backupFile;

    const auto bank = bankText(task->bank);
    const auto locations = locationsText();
    plan.question = "Overwrite " + locations + " on the synth?";

    const auto count = task->locations.size();
    const auto spacing = ", one every " + std::to_string(hardware::kBankWriteSpacingMs) + " ms.";
    if (task->operation == MemoryOperation::StoreProgram) {
        plan.details = (task->slot ? std::string("Slot ") + slotLetter(*task->slot) + "'s Program"
                                   : inQuotes(task->sourceName))
                       + " replaces " + locations + ".";
    } else if (task->operation == MemoryOperation::SendPrograms) {
        plan.details = (count == 1 ? std::string("1 Program") : std::to_string(count) + " Programs") + " of "
                       + task->sourceName + (count == 1 ? " replaces " : " replace ") + locations
                       + (count == 1 ? std::string(".") : spacing) + " The other Programs of " + bank
                       + " are not touched.";
    } else {
        plan.details = "The " + std::to_string(count) + " Programs of " + inQuotes(task->sourceName) + " replace "
                       + locations + spacing;
        if (task->skippedKits > 0)
            plan.details += " Its " + std::to_string(task->skippedKits)
                            + " Percussion Kits are not sent: storing Kits in a Bank is not supported yet.";
    }
    if (selectedBankUnreadable())
        plan.details += "\n\nThe synth stores what it receives in the Bank selected on its panel. Before you "
                        "confirm, select a Program in " + bank + " on the synth's panel, even if " + bank
                        + " is shown already" + notOnStoredSlot() + ", and do not change it until this is done. "
                        + sentDumpReason();
    else
        plan.details += "\n\nThe synth stores what it receives in the Bank selected on its panel: select " + bank
                        + " on the synth (any Program in it) before you confirm, and do not change it until this is "
                          "done.";
    if (current.backupFile)
        plan.details += "\n\nA backup of " + bank + " was saved to " + current.backupFile->string() + ".";
    else
        plan.details += "\n\nNo backup can be made: reading Banks from the synth is not verified on hardware yet.";

    if (plan.needsNoBackupConsent)
        plan.noBackupWarning = "No backup possible. The editor cannot save what is in " + locations
                               + " before overwriting it, because reading Banks from the synth is not verified "
                                 "yet. What is overwritten is lost unless you have your own copy.\n\nWrite "
                               + locations + " without a backup?";

    current.stage = MemoryStage::AwaitingConfirmation;
    current.done = 0;
    current.total = 0;
    current.plan = std::move(plan);
    current.message = std::move(message);
    changed();
}

MemoryConfirmResult MemoryWriter::confirm(std::uint64_t token, NoBackupConsent consent, TimeMs timeMs)
{
    if (current.stage != MemoryStage::AwaitingConfirmation || ! current.plan || current.plan->token != token)
        return MemoryConfirmResult::RefusedNoSuchPlan;
    if (current.plan->needsNoBackupConsent && consent != NoBackupConsent::Given)
        return MemoryConfirmResult::RefusedNoBackupConsent;

    current.plan.reset();
    if (task->slot) {
        // The Program as the Slot holds it now, edits made while the question was open included.
        const auto& program = engine.slotState(*task->slot).program;
        if (! program) {
            fail(std::string("Slot ") + slotLetter(*task->slot)
                 + " no longer plays a Program. Nothing was written to the synth.");
            return MemoryConfirmResult::Accepted;
        }
        task->programs = { *program };
    }

    if (selectedBankUnreadable()) {
        const auto bank = bankText(task->bank);
        offerPlan("The editor cannot read which Bank is selected on the synth yet. Select a Program in " + bank
                  + " on the synth's panel" + notOnStoredSlot() + ", then confirm again. Nothing was written. "
                  + sentDumpReason());
    } else if (hardware::kPerformanceNamesSelectedBank) {
        checkSelectedBank(timeMs);
    }
    else
        startWriting(timeMs);
    return MemoryConfirmResult::Accepted;
}

bool MemoryWriter::selectedBankUnreadable() const
{
    return hardware::kPerformanceNamesSelectedBank && performanceEditBufferHoldsSentDump;
}

std::string MemoryWriter::notOnStoredSlot() const
{
    // Selecting a Program on the Slot being stored would replace the sound about to be stored.
    if (! task || ! task->slot)
        return {};
    return std::string(" (on a Slot other than ") + slotLetter(*task->slot) + ", whose sound is the one stored)";
}

std::string MemoryWriter::sentDumpReason()
{
    return "(The editor sent the synth a Performance, when the project opened or for an Audition; the synth keeps "
           "it without playing it, so until a sound is selected on the synth the editor reads that Performance "
           "instead of the Banks the Slots play.)";
}

void MemoryWriter::performanceDumpSent()
{
    if (! performanceDumpsApplied)
        performanceEditBufferHoldsSentDump = true;
}

void MemoryWriter::soundSelectedOnSynth()
{
    performanceEditBufferHoldsSentDump = false;
}

void MemoryWriter::checkSelectedBank(TimeMs timeMs)
{
    const auto channel = engine.globalChannel();
    if (! channel) {
        fail("The connection to the synth was lost. Nothing was written to the synth.");
        return;
    }
    current.stage = MemoryStage::CheckingSelectedBank;
    changed();
    connection.enqueue(performanceEditBufferRequest(*channel), isPerformanceReply,
                       [this](auto reply, TimeMs doneMs) { selectedBankChecked(reply, doneMs); }, timeMs,
                       kMemoryWriterOwner);
}

void MemoryWriter::selectedBankChecked(std::optional<std::span<const std::uint8_t>> reply, TimeMs timeMs)
{
    if (! task)
        return;
    const auto target = bankText(task->bank);
    if (! reply) {
        offerPlan("The synth did not say which Bank is selected (no answer to the Performance request). Nothing "
                  "was written.");
        return;
    }
    const auto performance = parsePerformanceDump(*reply)->performance;
    const auto active = performance.setting(PerformanceSetting::SelectedSlot);
    if (active < 0 || active >= static_cast<int>(kAllSlots.size())) {
        offerPlan("The synth did not say which Bank is selected (active Slot " + std::to_string(active)
                  + "). Nothing was written.");
        return;
    }
    const auto selected = performance.setting(PerformanceSetting::ProgramBank, kAllSlots[static_cast<std::size_t>(active)]);
    if (selected != task->bank) {
        offerPlan(bankText(selected) + " is selected on the synth, not " + target + ". Select " + target
                  + " on the synth, then confirm again. Nothing was written.");
        return;
    }
    startWriting(timeMs);
}

void MemoryWriter::startWriting(TimeMs timeMs)
{
    current.stage = MemoryStage::Writing;
    current.done = 0;
    current.total = static_cast<int>(task->locations.size());
    task->written = 0;
    task->nextWriteMs = timeMs;
    changed();
    writeDue(timeMs);
}

void MemoryWriter::writeDue(TimeMs timeMs)
{
    // Dumps wait while another tool talks to the synth, and until no reply is expected.
    if (engine.requestsSuspended() || ! connection.isIdle(timeMs) || timeMs < task->nextWriteMs)
        return;
    const auto channel = engine.globalChannel();
    if (! channel) {
        fail("The connection to the synth was lost after " + std::to_string(task->written) + " of "
             + std::to_string(task->locations.size()) + " Programs were written.");
        return;
    }

    const auto index = static_cast<std::size_t>(task->written);
    const auto location = static_cast<std::uint8_t>(task->locations[index]);
    // The type byte names the manual's SysEx Bank; the synth ignores it and stores into the Bank
    // selected on its panel [OFF-2X p.78], which was checked above.
    const auto type = static_cast<std::uint8_t>(hardware::kFirstProgramBankDumpType
                                                + hardware::sysExBankOfShownBank(task->bank) - 1);
    connection.sendWithoutReply(bankProgramDump(*channel, type, location, task->programs[index].bytes()), timeMs);
    ++task->written;
    current.done = task->written;
    task->nextWriteMs = timeMs + hardware::kBankWriteSpacingMs;
    if (listener)
        listener->programWritten(task->bank, location + 1, task->programs[index]);
    changed();

    if (task->written < static_cast<int>(task->locations.size()))
        return;
    if (reading.verified) {
        current.stage = MemoryStage::Verifying;
        current.done = 0;
        changed();
    } else {
        verified(timeMs);
    }
}

void MemoryWriter::verified(TimeMs)
{
    const auto count = std::to_string(task->locations.size());
    std::string message;
    if (task->operation == MemoryOperation::StoreProgram)
        message = "Stored "
                  + (task->slot ? std::string("Slot ") + slotLetter(*task->slot) + "'s Program" : inQuotes(task->sourceName))
                  + " as " + locationsText() + ".";
    else
        message = "Sent " + count + (task->locations.size() == 1 ? " Program to " : " Programs to ") + locationsText()
                  + ".";
    if (reading.verified)
        message += " Read back: " + std::to_string(task->matches) + " of " + count + " match what was sent.";
    if (current.backupFile)
        message += " Backup: " + current.backupFile->string() + ".";
    else
        message += " No backup was made.";
    finish(message);
}

void MemoryWriter::advanceTime(TimeMs timeMs)
{
    if (! task)
        return;
    if (current.stage == MemoryStage::Writing) {
        writeDue(timeMs);
    } else if (current.stage == MemoryStage::Verifying && ! task->verifyStarted && timeMs >= task->nextWriteMs) {
        // The last dump has had its time to be stored before it is read back.
        task->verifyStarted = true;
        current.total = static_cast<int>(task->locations.size());
        readLocation(task->locations.front(), timeMs);
    }
}

void MemoryWriter::cancel()
{
    if (! busy())
        return;
    connection.cancel(kMemoryWriterOwner);
    const bool partlyWritten = task && task->written > 0 && current.stage == MemoryStage::Writing;
    std::string message = partlyWritten ? "Stopped after " + std::to_string(task->written) + " of "
                                              + std::to_string(task->locations.size()) + " Programs were written to "
                                              + bankText(task->bank) + "; the others are unchanged."
                                        : "Cancelled. Nothing was written to the synth.";
    if (current.backupFile)
        message += " Backup: " + current.backupFile->string() + ".";
    task.reset();
    current.stage = MemoryStage::Idle;
    current.plan.reset();
    current.message = std::move(message);
    ended();
}

void MemoryWriter::abort(const std::string& reason)
{
    if (! busy())
        return;
    connection.cancel(kMemoryWriterOwner);
    const bool partlyWritten = task && task->written > 0 && current.stage == MemoryStage::Writing;
    fail(reason + (partlyWritten ? " " + std::to_string(task->written) + " of "
                                       + std::to_string(task->locations.size())
                                       + " Programs had been written."
                                 : " Nothing was written to the synth."));
}

void MemoryWriter::selectionChangedOnSynth()
{
    if (current.stage == MemoryStage::CheckingSelectedBank || current.stage == MemoryStage::Writing)
        abort("A Program or Bank was selected on the synth during the write, so it was stopped.");
}

void MemoryWriter::finish(std::string message)
{
    task.reset();
    current.stage = MemoryStage::Finished;
    current.plan.reset();
    current.message = std::move(message);
    ended();
}

void MemoryWriter::fail(std::string message)
{
    if (current.backupFile)
        message += " Backup: " + current.backupFile->string() + ".";
    task.reset();
    current.stage = MemoryStage::Failed;
    current.plan.reset();
    current.message = std::move(message);
    ended();
}

void MemoryWriter::ended()
{
    if (listener)
        listener->taskEnded();
    changed();
}

} // namespace nl2x
