#include "nl2x/Librarian.h"

#include "nl2x/Engine.h"
#include "nl2x/SysEx.h"

#include <algorithm>
#include <utility>
#include <variant>

namespace nl2x {

namespace {

bool isRamBank(int bank)
{
    return bank >= hardware::kFirstRamProgramBank && bank <= hardware::kLastRamProgramBank;
}

std::vector<int> allRamBanks()
{
    std::vector<int> banks;
    for (int bank = hardware::kFirstRamProgramBank; bank <= hardware::kLastRamProgramBank; ++bank)
        banks.push_back(bank);
    return banks;
}

std::size_t index(int program)
{
    return static_cast<std::size_t>(program - 1);
}

} // namespace

std::string librarianRefusalText(LibrarianRefusal refusal)
{
    switch (refusal) {
    case LibrarianRefusal::None:
        return {};
    case LibrarianRefusal::NotARamBank:
        return "Only Banks 0–3 can be composed (Banks 4–9 are ROM).";
    case LibrarianRefusal::ProgramOutOfRange:
        return "A Bank holds Programs 1–99.";
    case LibrarianRefusal::BankNotRead:
        return "Read the synth's Banks, or load a Bank file, before composing this Bank.";
    case LibrarianRefusal::NoProgramThere:
        return "That location holds no known Program.";
    case LibrarianRefusal::NotAProgram:
        return "Only Programs can go into a Program location (Percussion Kits and Performances are not supported "
               "yet).";
    case LibrarianRefusal::NotAProgramBank:
        return "Choose a Program Bank file (a file with several Programs) in the Library.";
    case LibrarianRefusal::NoProgramsInFile:
        return "The file holds no Programs.";
    case LibrarianRefusal::TooManyProgramsInFile:
        return "The file holds more than 99 Programs, more than a Bank.";
    case LibrarianRefusal::BankIncomplete:
        return "Some Programs of this Bank are not known yet: read the synth's Banks first.";
    case LibrarianRefusal::FileExists:
        return "The file already exists and was not replaced.";
    case LibrarianRefusal::SaveFailed:
        return "The file could not be written.";
    }
    return {};
}

Librarian::Librarian(const Engine& owner, MemoryWriter& memoryWriter, std::function<void()> changedCallback)
    : engine(owner),
      memory(memoryWriter),
      onChange(std::move(changedCallback))
{
}

bool Librarian::valid(BankLocation location)
{
    return isRamBank(location.bank) && location.program >= 1 && location.program <= kProgramsPerBank;
}

MemoryRefusal Librarian::readSynthBanks(TimeMs timeMs)
{
    const auto refusal = memory.readBanks(allRamBanks(), timeMs);
    if (refusal != MemoryRefusal::None)
        return refusal;
    readStage = SynthBanksStage::Reading;
    readMessage.clear();
    changed();
    return refusal;
}

SynthBanksStatus Librarian::synthBanksStatus() const
{
    SynthBanksStatus status;
    status.stage = readStage;
    status.message = readMessage;
    if (readStage == SynthBanksStage::Reading) {
        status.done = memory.status().done;
        status.total = memory.status().total;
    }
    return status;
}

bool Librarian::synthBankRead(int bank) const
{
    if (! isRamBank(bank))
        return false;
    const auto& synth = bankAt(bank).synth;
    return std::all_of(synth.begin(), synth.end(), [](const auto& program) { return program.has_value(); });
}

std::optional<Program> Librarian::synthProgram(BankLocation location) const
{
    if (! valid(location))
        return std::nullopt;
    return bankAt(location.bank).synth[index(location.program)];
}

std::optional<Program> Librarian::program(BankLocation location) const
{
    if (! valid(location))
        return std::nullopt;
    const auto& bank = bankAt(location.bank);
    if (const auto change = bank.changes.find(location.program - 1); change != bank.changes.end())
        return change->second;
    return bank.synth[index(location.program)];
}

bool Librarian::isPending(BankLocation location) const
{
    return valid(location) && bankAt(location.bank).changes.contains(location.program - 1);
}

std::vector<int> Librarian::pendingPrograms(int bank) const
{
    std::vector<int> programs;
    if (isRamBank(bank))
        for (const auto& [location, program] : bankAt(bank).changes)
            programs.push_back(location + 1);
    return programs;
}

std::optional<std::string> Librarian::wholeBankSource(int bank) const
{
    if (! isRamBank(bank))
        return std::nullopt;
    return bankAt(bank).wholeBankSource;
}

bool Librarian::anythingKnown(int bank) const
{
    const auto& state = bankAt(bank);
    return ! state.changes.empty()
           || std::any_of(state.synth.begin(), state.synth.end(), [](const auto& program) { return program.has_value(); });
}

LibrarianRefusal Librarian::put(BankLocation location, const Program& value)
{
    if (! isRamBank(location.bank))
        return LibrarianRefusal::NotARamBank;
    if (! valid(location))
        return LibrarianRefusal::ProgramOutOfRange;
    if (! anythingKnown(location.bank))
        return LibrarianRefusal::BankNotRead;
    bankAt(location.bank).changes[location.program - 1] = value;
    changed();
    return LibrarianRefusal::None;
}

LibrarianRefusal Librarian::put(BankLocation location, const LibraryEntry& entry)
{
    const auto* dump = std::get_if<ProgramDump>(&entry.dump);
    if (! dump)
        return LibrarianRefusal::NotAProgram;
    return put(location, dump->program);
}

LibrarianRefusal Librarian::copy(BankLocation from, BankLocation to)
{
    if (! isRamBank(from.bank))
        return LibrarianRefusal::NotARamBank;
    if (! valid(from))
        return LibrarianRefusal::ProgramOutOfRange;
    const auto source = program(from);
    if (! source)
        return LibrarianRefusal::NoProgramThere;
    return put(to, *source);
}

void Librarian::undo(BankLocation location)
{
    if (! valid(location))
        return;
    auto& bank = bankAt(location.bank);
    if (bank.changes.erase(location.program - 1) == 0)
        return;
    if (bank.changes.empty())
        bank.wholeBankSource.reset();
    changed();
}

void Librarian::discardChanges(int bank)
{
    if (! isRamBank(bank))
        return;
    auto& state = bankAt(bank);
    state.changes.clear();
    state.wholeBankSource.reset();
    changed();
}

LibrarianRefusal Librarian::replaceWithFile(int bank, const LibraryFile& file)
{
    if (! isRamBank(bank))
        return LibrarianRefusal::NotARamBank;
    if (file.kind != LibraryFileKind::ProgramBank)
        return LibrarianRefusal::NotAProgramBank;
    std::map<int, Program> changes;
    for (const auto& entry : file.entries) {
        const auto* dump = std::get_if<ProgramDump>(&entry.dump);
        if (! dump)
            continue;
        const auto location = static_cast<int>(changes.size());
        if (location == kProgramsPerBank)
            return LibrarianRefusal::TooManyProgramsInFile;
        changes[location] = dump->program;
    }
    if (changes.empty())
        return LibrarianRefusal::NoProgramsInFile;
    auto& state = bankAt(bank);
    state.changes = std::move(changes);
    state.wholeBankSource = file.name;
    changed();
    return LibrarianRefusal::None;
}

LibrarianRefusal Librarian::saveBankFile(int bank, const std::filesystem::path& file, ReplaceExisting replace) const
{
    if (! isRamBank(bank))
        return LibrarianRefusal::NotARamBank;
    std::vector<Program> programs;
    bool gap = false;
    for (int number = 1; number <= kProgramsPerBank; ++number) {
        const auto value = program({ bank, number });
        if (! value) {
            gap = true;
            continue;
        }
        if (gap)
            return LibrarianRefusal::BankIncomplete;
        programs.push_back(*value);
    }
    if (programs.empty())
        return LibrarianRefusal::BankNotRead;

    // As the synth's own replies for this Bank, which "Read Bank into file" keeps as received.
    const auto channel = engine.globalChannel().value_or(hardware::kFactoryGlobalChannel);
    const auto type = static_cast<std::uint8_t>(hardware::kFirstProgramBankDumpType
                                                + hardware::sysExBankOfShownBank(bank) - 1);
    std::vector<std::uint8_t> bytes;
    for (std::size_t location = 0; location < programs.size(); ++location) {
        const auto dump = bankProgramDump(channel, type, static_cast<std::uint8_t>(location), programs[location].bytes());
        bytes.insert(bytes.end(), dump.begin(), dump.end());
    }
    switch (saveLibraryFile(file, bytes, replace)) {
    case LibrarySaveResult::Saved:
        return LibrarianRefusal::None;
    case LibrarySaveResult::AlreadyExists:
        return LibrarianRefusal::FileExists;
    case LibrarySaveResult::Failed:
        return LibrarianRefusal::SaveFailed;
    }
    return LibrarianRefusal::SaveFailed;
}

MemoryRefusal Librarian::prepareSend(int bank, const std::filesystem::path& backupFolder, WallClockTime now,
                                     TimeMs timeMs)
{
    if (! isRamBank(bank))
        return MemoryRefusal::NotARamBank;
    const auto& state = bankAt(bank);
    std::vector<ProgramWrite> writes;
    for (const auto& [location, value] : state.changes)
        writes.push_back({ location + 1, value });
    const auto source = state.wholeBankSource ? "\"" + *state.wholeBankSource + "\""
                                              : "the Librarian's Bank " + std::to_string(bank);
    return memory.prepareProgramWrites(bank, std::move(writes), source, backupFolder, now, timeMs);
}

MemoryRefusal Librarian::prepareStore(const LibraryEntry& entry, BankLocation location,
                                      const std::filesystem::path& backupFolder, WallClockTime now, TimeMs timeMs)
{
    const auto* dump = std::get_if<ProgramDump>(&entry.dump);
    if (! dump)
        return MemoryRefusal::NotAProgram;
    return memory.prepareStoreProgram(dump->program, entry.name, location.bank, location.program, backupFolder, now,
                                      timeMs);
}

std::optional<SoundMatch> Librarian::soundMatch(const LibraryMetadata& metadata, BankLocation location) const
{
    const auto value = program(location);
    if (! value)
        return std::nullopt;
    return metadata.match(SoundFingerprint::of(*value));
}

SoundInfo Librarian::soundInfo(const LibraryMetadata& metadata, BankLocation location) const
{
    const auto value = program(location);
    if (! value)
        return {};
    return metadata.info(SoundFingerprint::of(*value));
}

void Librarian::programRead(int bank, int number, const Program& value)
{
    if (! isRamBank(bank) || number < 1 || number > kProgramsPerBank)
        return;
    bankAt(bank).synth[index(number)] = value;
    changed();
}

void Librarian::programWritten(int bank, int number, const Program& value)
{
    if (! isRamBank(bank) || number < 1 || number > kProgramsPerBank)
        return;
    auto& state = bankAt(bank);
    state.synth[index(number)] = value;
    // Sent: no longer pending, unless the user put something else there meanwhile.
    if (const auto change = state.changes.find(number - 1); change != state.changes.end() && change->second == value) {
        state.sent[change->first] = change->second;
        state.changes.erase(change);
        if (state.changes.empty() && state.wholeBankSource) {
            state.sentWholeBankSource = state.wholeBankSource;
            state.wholeBankSource.reset();
        }
    }
    changed();
}

void Librarian::programNotStored(int bank, int number)
{
    if (! isRamBank(bank) || number < 1 || number > kProgramsPerBank)
        return;
    auto& state = bankAt(bank);
    // Pending again, unless the user put something else there meanwhile.
    if (const auto sent = state.sent.find(number - 1); sent != state.sent.end()) {
        if (state.changes.emplace(sent->first, sent->second).second && ! state.wholeBankSource)
            state.wholeBankSource = state.sentWholeBankSource;
        state.sent.erase(sent);
    }
    changed();
}

void Librarian::taskEnded()
{
    for (auto& state : banks) {
        state.sent.clear();
        state.sentWholeBankSource.reset();
    }
    if (readStage != SynthBanksStage::Reading)
        return;
    const auto& status = memory.status();
    if (status.stage == MemoryStage::Finished) {
        readStage = SynthBanksStage::Read;
        readMessage.clear();
    } else {
        readStage = SynthBanksStage::Failed;
        readMessage = status.message;
    }
    changed();
}

} // namespace nl2x
