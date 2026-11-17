#include "nl2x/MemoryWriter.h"

#include "PerformanceEditBuffer.h"
#include "SynthConnection.h"

#include "nl2x/Engine.h"
#include "nl2x/SlotChannels.h"
#include "nl2x/SysEx.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <numeric>
#include <utility>
#include <variant>

namespace nl2x {

namespace {

constexpr SynthConnection::Owner kMemoryWriterOwner = 2;
constexpr int kProgramsPerBank = hardware::kProgramsPerRamBank;
constexpr std::uint8_t kControlChange = 0xB0;
constexpr std::uint8_t kProgramChange = 0xC0;

// A reply to a Bank location request: a Program dump, matched by size and never by header
// (research §3), whose data decodes without loss.
bool isProgramDumpReply(std::span<const std::uint8_t> message)
{
    return message.size() == kProgramDumpSize && parseProgramDump(message).has_value();
}

// A reply to a Slot Edit Buffer request: a Program or a Kit, matched by size (HardwareAssumptions.h).
bool isSlotEditBufferReply(std::span<const std::uint8_t> message)
{
    if (hardware::classifySlotEditBufferReply(message.size()) == hardware::SlotEditBufferReply::NotAReply)
        return false;
    return parseSysEx(message).values.has_value();
}

std::string bankText(int bank)
{
    return "Bank " + std::to_string(bank);
}

std::string slotText(Slot slot)
{
    return std::string("Slot ") + slotLetter(slot);
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

// "Slot C", "Slots C and D"
std::string slotsText(const std::vector<Slot>& slots)
{
    std::string text = slots.size() == 1 ? "Slot " : "Slots ";
    for (std::size_t i = 0; i < slots.size(); ++i)
        text += std::string(i == 0 ? "" : i + 1 == slots.size() ? " and " : ", ") + slotLetter(slots[i]);
    return text;
}

constexpr const char* kCannotSwitch = "The editor cannot do this itself: ";

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

MemoryWriter::MemoryWriter(const Engine& owner, SynthConnection& synthConnection, MidiOutput& midiOutput,
                           const hardware::Switches& switches, std::function<void()> changedCallback)
    : engine(owner),
      connection(synthConnection),
      output(midiOutput),
      reading { switches.bankReadingVerified, switches.bankRequestTypeOfSysExBank1 },
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
    return task && task->confirmed && busy();
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
        readLitSlot(timeMs);
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
        const bool stored = readBack == task->programs[index];
        if (stored)
            ++task->matches;
        else
            task->notStored.push_back(location + 1);
        if (listener) {
            if (! stored)
                listener->programNotStored(task->bank, location + 1);
            listener->programRead(task->bank, location + 1, readBack);
        }
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

void MemoryWriter::backupComplete(TimeMs timeMs)
{
    // Written now, before any confirmation is asked for, so it exists whatever happens next.
    const auto path = uniqueLibraryPath(task->backupFolder, backupName(task->bank, task->time));
    if (saveLibraryFile(path, joined(task->replies)) != LibrarySaveResult::Saved) {
        fail("Could not write the backup file " + path.string() + ". Nothing was written to the synth.");
        return;
    }
    current.backupFile = path;
    readLitSlot(timeMs);
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

// ---------------------------------------------------------------------------------------------
// The lit Slot: which Bank the synth will store into, and switching it for the write.

void MemoryWriter::readPerformance(TimeMs timeMs, PerformanceRead then)
{
    const auto channel = engine.globalChannel();
    if (! channel) {
        fail("The connection to the synth was lost." + notPutBackText());
        return;
    }
    connection.enqueue(
        performanceEditBufferRequest(*channel), PerformanceEditBuffer::isReply,
        [this, then = std::move(then)](auto reply, TimeMs doneMs) {
            if (! task)
                return;
            std::optional<Performance> performance;
            if (reply)
                if (const auto dump = parsePerformanceDump(*reply))
                    performance = dump->performance;
            then(performance, doneMs);
        },
        timeMs, kMemoryWriterOwner);
}

void MemoryWriter::readLitSlot(TimeMs timeMs)
{
    current.stage = MemoryStage::ReadingLitSlot;
    current.done = 0;
    current.total = 0;
    changed();
    readPerformance(timeMs, [this](const auto& performance, TimeMs doneMs) { litSlotRead(performance, doneMs); });
}

MemoryWriter::BankDecision MemoryWriter::decide(const std::optional<Performance>& performance) const
{
    BankDecision decision;
    const auto byHand = [&decision](std::string reason) {
        decision.setting = BankSetting::ByHand;
        decision.reason = kCannotSwitch + std::move(reason);
        return decision;
    };
    if (! engine.performanceEditBufferReadTrusted())
        return byHand("it sent the synth a Performance (a Performance Audition), and the synth reports that "
                      "Performance instead of telling which Bank the lit Slot is on.");
    if (! performance)
        return byHand("the synth did not tell which Bank the lit Slot is on.");

    const auto selected = performance->setting(PerformanceSetting::SelectedSlot);
    if (selected < 0 || selected >= static_cast<int>(kAllSlots.size()))
        return byHand("the synth did not tell which Slot is lit.");
    const auto slot = kAllSlots[static_cast<std::size_t>(selected)];
    const LitSlot lit { slot, performance->setting(PerformanceSetting::ProgramBank, slot),
                        performance->setting(PerformanceSetting::ProgramNumber, slot) };
    const int lastNumber = hardware::kProgramChangeOfPercussionKitP0 + hardware::kPercussionKitsPerBank - 1;
    if (lit.bank < 0 || lit.bank >= hardware::kProgramBankCount || lit.programNumber < 0
        || lit.programNumber > lastNumber)
        return byHand("the synth names a Bank or Program for the lit Slot that the editor does not know.");
    decision.lit = lit;

    const auto name = "the lit Slot, " + slotText(slot) + ", ";
    if (lit.programNumber >= hardware::kProgramChangeOfPercussionKitP0
        || engine.slotState(slot).content == SlotContent::PercussionKit)
        return byHand(name + "plays a Percussion Kit, which the editor cannot put back exactly afterwards.");
    if (lit.bank == task->bank) {
        decision.setting = BankSetting::AlreadyThere;
        return decision;
    }

    // The channel the lit Slot hears a Program Change on, and who else hears it there.
    SlotChannelArray channels;
    SlotLayerArray layer {};
    for (auto each : kAllSlots) {
        const auto index = slotIndex(each);
        channels[index] = engine.slotChannelOverride(each)
                              ? engine.slotChannelOverride(each)
                              : hardware::slotChannelFromPerformance(
                                    performance->bytes()[performanceSettingOffset(PerformanceSetting::SlotChannel, each)]);
        layer[index] = hardware::kProgramChangeReachesEverySlotReceivingOnItsChannel
                       && performance->setting(PerformanceSetting::Layer, each) != 0;
    }
    const auto channel = channels[slotIndex(slot)];
    if (! channel)
        return byHand(name + "has its MIDI channel off.");
    if (channel == engine.globalChannel())
        return byHand(name + "is on MIDI channel " + std::to_string(*channel + 1)
                      + ", the Global Channel, where a Program Change selects a Performance.");
    std::vector<Slot> others;
    for (auto reached : slotsReceivingOn(*channel, channels, layer))
        if (reached != slot)
            others.push_back(reached);
    if (! others.empty())
        return byHand("a Program Change on the lit Slot's MIDI channel (" + std::to_string(*channel + 1)
                      + ") would also change " + slotsText(others) + ".");
    decision.setting = BankSetting::SwitchedByEditor;
    decision.channel = *channel;
    return decision;
}

void MemoryWriter::litSlotRead(const std::optional<Performance>& performance, TimeMs timeMs)
{
    auto decision = decide(performance);
    const auto decided = [this, performance](BankDecision made, TimeMs doneMs) {
        task->decision = std::move(made);
        if (! task->confirmed) {
            offerPlan();
            return;
        }
        if (task->decision.lit)
            task->slotsAfterOverwrite = performance;
        const auto& now = task->decision;
        switch (now.setting) {
        case BankSetting::AlreadyThere:
            startWriting(doneMs);
            return;
        case BankSetting::SwitchedByEditor:
            startSwitching(doneMs);
            return;
        case BankSetting::ByHand:
            // A read checked against the lit Slot says the user did not set the target Bank: nothing
            // is written. Without such a read the write goes ahead and the read-back tells.
            if (now.checked && now.lit && now.lit->bank != task->bank) {
                fail("Nothing was written: the lit Slot, " + slotText(now.lit->slot) + ", is on "
                     + bankText(now.lit->bank) + ", not " + bankText(task->bank) + ". On the synth, set the lit Slot to "
                     + bankText(task->bank) + ", then try again.");
                return;
            }
            startWriting(doneMs);
            return;
        }
    };

    // A Performance the synth kept from an earlier dump (kPerformanceEditBufferDumpIsApplied) would
    // name a Slot and Bank of its own: the Performance's copy of the lit Slot's Program must equal
    // the lit Slot's Edit Buffer [HW 2026-11-16: they are equal when nothing was sent].
    const bool checkable = decision.lit && decision.lit->programNumber < hardware::kProgramChangeOfPercussionKitP0
                           && engine.slotState(decision.lit->slot).content != SlotContent::PercussionKit;
    const auto channel = engine.globalChannel();
    if (! checkable || ! channel) {
        decided(std::move(decision), timeMs);
        return;
    }
    const auto slot = decision.lit->slot;
    const auto copy = performance->program(slot);
    connection.enqueue(
        slotEditBufferRequest(*channel, slot), isSlotEditBufferReply,
        [this, decision, copy, decided](auto reply, TimeMs doneMs) mutable {
            if (! task)
                return;
            std::optional<Program> editBuffer;
            if (reply)
                if (const auto dump = parseProgramDump(*reply))
                    editBuffer = dump->program;
            if (editBuffer && *editBuffer == copy) {
                decision.checked = true;
                task->savedEditBuffer = editBuffer;
            } else {
                const auto slotName = slotText(decision.lit->slot);
                decision = {};
                decision.reason = std::string(kCannotSwitch) + "what the synth reports about the lit Slot does not "
                                  "match " + slotName + "'s sound, so it may be out of date.";
            }
            decided(std::move(decision), doneMs);
        },
        timeMs, kMemoryWriterOwner);
}

void MemoryWriter::offerPlan()
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
    plan.bankSetting = task->decision.setting;

    const auto bank = bankText(task->bank);
    const auto locations = locationsText();
    plan.question = "Overwrite " + locations + " on the synth?";
    plan.panelSteps = panelStepsText();

    const auto count = task->locations.size();
    const auto spacing = ", one every " + std::to_string(hardware::kBankWriteSpacingMs) + " ms.";
    if (task->operation == MemoryOperation::StoreProgram) {
        plan.details = (task->slot ? slotText(*task->slot) + "'s Program" : inQuotes(task->sourceName)) + " replaces "
                       + locations + ".";
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
    if (reading.verified)
        plan.details += " Afterwards the editor reads " + locations + " back to check that the write arrived there.";
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
    current.message.clear();
    changed();
}

std::string MemoryWriter::panelStepsText() const
{
    const auto& decision = task->decision;
    const auto bank = bankText(task->bank);
    constexpr const char* kHandsOff = "Do not play or touch the synth until the write is done.";
    if (decision.setting == BankSetting::AlreadyThere)
        return "The lit Slot, " + slotText(decision.lit->slot) + ", is on " + bank + " already, where the synth "
               "stores what it receives. " + kHandsOff;
    if (decision.setting == BankSetting::SwitchedByEditor) {
        const auto& lit = *decision.lit;
        return "The synth stores what it receives in the Bank of the lit Slot, " + slotText(lit.slot) + ", which is on "
               + bankText(lit.bank) + ". The editor will switch the lit Slot to " + bank
               + " for a moment, write, and then put it back on " + bankText(lit.bank) + " Program "
               + std::to_string(lit.programNumber + 1) + ", with its unsaved edits. " + kHandsOff;
    }

    std::vector<std::string> steps;
    // Selecting a Program on the Slot being stored would replace the sound about to be stored.
    if (task->slot && (! decision.lit || decision.lit->slot == *task->slot)) {
        const auto letter = std::string(1, slotLetter(*task->slot));
        steps.push_back("Press a Slot button other than " + letter + ", so that another Slot is lit (Slot " + letter
                        + " holds the sound being stored).");
    }
    steps.push_back("Set the lit Slot to " + bank + ": select any Program in " + bank + ".");
    steps.push_back(std::string("Then click Overwrite. ") + kHandsOff);
    std::string text = "The synth stores what it receives in the Bank of the lit Slot. On the synth:";
    for (std::size_t i = 0; i < steps.size(); ++i)
        text += "\n" + std::to_string(i + 1) + ". " + steps[i];
    if (! decision.reason.empty())
        text += "\n\n" + decision.reason;
    return text;
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
            fail(slotText(*task->slot) + " no longer plays a Program. Nothing was written to the synth.");
            return MemoryConfirmResult::Accepted;
        }
        task->programs = { *program };
    }
    task->confirmed = true;
    // The lit Slot again: the user may have changed it, or set the Bank by hand, since the question.
    task->savedEditBuffer.reset();
    readLitSlot(timeMs);
    return MemoryConfirmResult::Accepted;
}

void MemoryWriter::startSwitching(TimeMs timeMs)
{
    task->lit = task->decision.lit;
    task->litChannel = task->decision.channel;
    current.stage = MemoryStage::SwitchingBank;
    current.done = 0;
    current.total = 0;
    task->step = Task::Step::SendSwitch;
    task->stepDueMs = timeMs;
    changed();
    stepDue(timeMs);
}

void MemoryWriter::sendSelection(int bank, int programNumber)
{
    // Bank Select first: it takes effect with the next Program Change (HardwareAssumptions.h).
    const auto channel = task->litChannel;
    const std::array<std::uint8_t, 3> bankSelect { static_cast<std::uint8_t>(kControlChange | channel),
                                                   hardware::kBankSelectController,
                                                   static_cast<std::uint8_t>(bank + hardware::kBankSelectOfProgramBank0) };
    const std::array<std::uint8_t, 2> programChange {
        static_cast<std::uint8_t>(kProgramChange | channel),
        static_cast<std::uint8_t>(programNumber + hardware::kProgramChangeOfProgram1)
    };
    output.send(bankSelect);
    output.send(programChange);
}

void MemoryWriter::stepDue(TimeMs timeMs)
{
    // Like every other message: not while another tool talks to the synth, nor while a reply is due.
    if (task->step == Task::Step::None || timeMs < task->stepDueMs || engine.requestsSuspended()
        || ! connection.isIdle(timeMs))
        return;
    const auto& lit = *task->lit;
    switch (task->step) {
    case Task::Step::None:
        return;
    case Task::Step::SendSwitch:
        // Any Program of the target Bank will do: the first one written.
        sendSelection(task->bank, task->locations.front());
        task->switched = true;
        task->step = Task::Step::CheckSwitch;
        task->stepDueMs = timeMs + hardware::kSelectionSettleMs;
        changed();
        return;
    case Task::Step::CheckSwitch:
        task->step = Task::Step::None;
        readPerformance(timeMs, [this](const auto& performance, TimeMs doneMs) { switchChecked(performance, doneMs); });
        return;
    case Task::Step::SendRestore:
        sendSelection(lit.bank, lit.programNumber);
        task->step = Task::Step::SendEditBuffer;
        task->stepDueMs = timeMs + hardware::kSelectionSettleMs;
        changed();
        return;
    case Task::Step::SendEditBuffer: {
        const auto channel = engine.globalChannel();
        if (! channel) {
            fail("The connection to the synth was lost." + notPutBackText());
            return;
        }
        // The unsaved edits come back with it (verified [HW 2026-11-16]).
        connection.sendWithoutReply(programEditBufferDump(*channel, lit.slot, task->savedEditBuffer->bytes()), timeMs);
        task->restored = true;
        task->step = Task::Step::None;
        const auto back = " The editor put " + slotText(lit.slot) + " back on " + litSlotOriginText()
                          + ", with its unsaved edits.";
        if (task->endAfterRestore) {
            auto message = *task->endAfterRestore + back;
            if (! task->cancelledAfterRestore) {
                fail(std::move(message));
                return;
            }
            if (current.backupFile)
                message += " Backup: " + current.backupFile->string() + ".";
            task.reset();
            current.stage = MemoryStage::Idle;
            current.plan.reset();
            current.message = std::move(message);
            ended();
            return;
        }
        startVerifying(timeMs);
        return;
    }
    }
}

void MemoryWriter::switchChecked(const std::optional<Performance>& performance, TimeMs timeMs)
{
    const auto slot = task->lit->slot;
    const bool there = performance
                       && performance->setting(PerformanceSetting::SelectedSlot) == static_cast<int>(slotIndex(slot))
                       && performance->setting(PerformanceSetting::ProgramBank, slot) == task->bank;
    if (there) {
        startWriting(timeMs);
        return;
    }
    const auto bank = bankText(task->bank);
    startRestoring("Nothing was written: the lit Slot, " + slotText(slot) + ", did not switch to " + bank
                       + ". Check that the synth receives Program Changes (the P setting, Shift + PROG/CTRL), or set "
                         "the lit Slot to "
                       + bank + " on the synth and try again.",
                   timeMs);
}

void MemoryWriter::startRestoring(std::optional<std::string> failure, TimeMs timeMs)
{
    task->endAfterRestore = std::move(failure);
    current.stage = MemoryStage::RestoringSlot;
    current.done = 0;
    current.total = 0;
    task->step = Task::Step::SendRestore;
    // The last dump written has had its time to be stored; advanceTime() sends the rest.
    task->stepDueMs = std::max(timeMs, task->nextWriteMs);
    changed();
}

std::string MemoryWriter::litSlotName() const
{
    return slotText(task->lit->slot);
}

std::string MemoryWriter::litSlotOriginText() const
{
    return bankText(task->lit->bank) + " Program " + std::to_string(task->lit->programNumber + 1);
}

std::string MemoryWriter::notPutBackText() const
{
    if (! task || ! task->switched || task->restored)
        return {};
    return " The lit Slot, " + litSlotName() + ", was not put back: on the synth it may still be on "
           + bankText(task->bank) + " instead of " + litSlotOriginText() + ", and its unsaved edits may be lost.";
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
             + std::to_string(task->locations.size()) + " Programs were written." + notPutBackText());
        return;
    }

    const auto index = static_cast<std::size_t>(task->written);
    const auto location = static_cast<std::uint8_t>(task->locations[index]);
    // The type byte names the manual's SysEx Bank; the synth ignores it and stores into the lit
    // Slot's Bank (kFirstProgramBankDumpType), which is the target Bank by now.
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
    if (task->switched)
        startRestoring(std::nullopt, timeMs);
    else
        startVerifying(timeMs);
}

void MemoryWriter::startVerifying(TimeMs timeMs)
{
    if (! reading.verified) {
        verified(timeMs);
        return;
    }
    current.stage = MemoryStage::Verifying;
    current.done = 0;
    changed();
}

void MemoryWriter::verified(TimeMs timeMs)
{
    const auto count = std::to_string(task->locations.size());
    if (! task->notStored.empty()) {
        // The usual cause: the lit Slot was on another Bank, and the dumps went there.
        const auto bank = bankText(task->bank);
        const bool one = task->notStored.size() == 1;
        const auto where = (one ? "Program " : "Programs ") + listed(task->notStored) + " in " + bank;
        fail("Not stored: " + where + (one ? " does not hold" : " do not hold") + " what was sent (read back: "
             + std::to_string(task->matches) + " of " + count + " match). The synth probably stored "
             + (one ? "it" : "them") + " in the Bank of the lit Slot instead, at the same Program "
             + (one ? "number" : "numbers") + ", replacing what was there; only " + bank
             + " was backed up. On the synth, set the lit Slot to " + bank + ", then try again.");
        return;
    }
    if (task->switched)
        checkRestoredSlot(timeMs);
    else
        finishWrite({});
}

void MemoryWriter::checkRestoredSlot(TimeMs timeMs)
{
    // The lit Slot's Edit Buffer, then its Bank and Program, as they were before the switch. (The
    // written locations are read back already, or not at all while Bank reading is locked.)
    current.stage = MemoryStage::Verifying;
    task->verifyStarted = true;
    changed();
    const auto channel = engine.globalChannel();
    if (! channel) {
        fail("The connection to the synth was lost.");
        return;
    }
    const auto slot = task->lit->slot;
    const auto notBack = " But " + litSlotName() + " did not come back exactly as it was: check it on the synth (it "
                                                   "should be on "
                         + litSlotOriginText() + ").";
    connection.enqueue(
        slotEditBufferRequest(*channel, slot), isSlotEditBufferReply,
        [this, slot, notBack](auto reply, TimeMs doneMs) {
            if (! task)
                return;
            std::optional<Program> editBuffer;
            if (reply)
                if (const auto dump = parseProgramDump(*reply))
                    editBuffer = dump->program;
            if (editBuffer != task->savedEditBuffer) {
                finishWrite(notBack);
                return;
            }
            readPerformance(doneMs, [this, slot, notBack](const auto& performance, TimeMs) {
                const auto& lit = *task->lit;
                const bool back = performance
                                  && performance->setting(PerformanceSetting::SelectedSlot)
                                         == static_cast<int>(slotIndex(slot))
                                  && performance->setting(PerformanceSetting::ProgramBank, slot) == lit.bank
                                  && performance->setting(PerformanceSetting::ProgramNumber, slot) == lit.programNumber;
                finishWrite(back ? " " + litSlotName() + " is back on " + litSlotOriginText() + ", with its unsaved edits."
                                 : notBack);
            });
        },
        timeMs, kMemoryWriterOwner);
}

void MemoryWriter::finishWrite(const std::string& slotNote)
{
    const auto count = std::to_string(task->locations.size());
    std::string message;
    if (task->operation == MemoryOperation::StoreProgram)
        message = "Stored " + (task->slot ? slotText(*task->slot) + "'s Program" : inQuotes(task->sourceName)) + " as "
                  + locationsText() + ".";
    else
        message = "Sent " + count + (task->locations.size() == 1 ? " Program to " : " Programs to ") + locationsText()
                  + ".";
    if (reading.verified)
        message += " Read back: " + std::to_string(task->matches) + " of " + count + " match what was sent.";
    message += slotNote;
    message += reloadSlotsOnWrittenLocations();
    if (current.backupFile)
        message += " Backup: " + current.backupFile->string() + ".";
    else
        message += " No backup was made.";
    finish(message);
}

std::string MemoryWriter::reloadSlotsOnWrittenLocations()
{
    if (! task->slotsAfterOverwrite)
        return {};
    const auto& performance = *task->slotsAfterOverwrite;
    std::vector<std::string> names;
    for (const auto slot : kAllSlots) {
        if (performance.setting(PerformanceSetting::ProgramBank, slot) != task->bank)
            continue;
        // Program numbers 0–98 are Programs 1–99, as the locations are; Kits come after them.
        const auto number = performance.setting(PerformanceSetting::ProgramNumber, slot);
        const auto found = std::find(task->locations.begin(), task->locations.end(), number);
        if (found == task->locations.end())
            continue;
        const auto& written = task->programs[static_cast<std::size_t>(found - task->locations.begin())];
        const auto& state = engine.slotState(slot);
        if (state.content != SlotContent::Program || state.program == written)
            continue;
        slotReloads.push_back({ slot, written });
        names.push_back(slotText(slot));
    }
    if (names.empty())
        return {};
    std::string list;
    for (std::size_t i = 0; i < names.size(); ++i)
        list += (i == 0 ? "" : i + 1 == names.size() ? " and " : ", ") + names[i];
    return " " + list + (names.size() == 1 ? " was" : " were")
           + " on a location just written, and now " + (names.size() == 1 ? "plays" : "play")
           + " the new sound.";
}

void MemoryWriter::advanceTime(TimeMs timeMs)
{
    if (! task)
        return;
    if (current.stage == MemoryStage::Writing) {
        writeDue(timeMs);
    } else if (current.stage == MemoryStage::SwitchingBank || current.stage == MemoryStage::RestoringSlot) {
        stepDue(timeMs);
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
    // Already on its way back: the lit Slot is put back first.
    if (current.stage == MemoryStage::RestoringSlot)
        return;
    connection.cancel(kMemoryWriterOwner);
    const bool partlyWritten = task && task->written > 0 && current.stage == MemoryStage::Writing;
    std::string message = partlyWritten ? "Stopped after " + std::to_string(task->written) + " of "
                                              + std::to_string(task->locations.size()) + " Programs were written to "
                                              + bankText(task->bank) + "; the others are unchanged."
                                        : "Cancelled. Nothing was written to the synth.";
    if (task && task->switched && ! task->restored) {
        // The lit Slot goes back to its Bank first; the task ends then.
        task->cancelledAfterRestore = true;
        startRestoring(std::move(message), task->nextWriteMs);
        return;
    }
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
    fail(reason
         + (partlyWritten ? " " + std::to_string(task->written) + " of " + std::to_string(task->locations.size())
                                + " Programs had been written."
                          : " Nothing was written to the synth.")
         + notPutBackText());
}

void MemoryWriter::selectionChangedOnSynth()
{
    if (current.stage == MemoryStage::Writing)
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
