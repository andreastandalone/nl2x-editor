#pragma once

// The memory writer (spec #1, user stories 58–63): stores a Slot's or a Library Program into a Bank
// location, sends a whole Bank file or a set of Programs (the Librarian's pending locations, #44)
// to the synth, reads a whole Bank into a .syx file and reads Banks for the Librarian.
//
// Nothing is ever written without a confirmation: prepareStoreProgram() and prepareBankSend()
// only read (the backup) and then hold a MemoryWritePlan naming exactly what will be overwritten;
// only confirm() with that plan's token writes. Every write goes one Program dump per location, so
// only the locations named in the plan change. The order is fixed: backup → confirmation → check
// of the Bank selected on the synth → write. While reading Banks is not verified on hardware
// (hardware::kBankReadingVerified, the lock), no backup can be made and every plan needs the
// user's separate "no backup possible" consent.
//
// The synth stores a received Bank dump in the Bank selected on its panel, whatever the dump's
// type byte says [OFF-2X p.78]. The writer does not select the Bank itself (no Program Change is
// sent, which would replace a Slot's sound and depends on the P setting): the user selects it on
// the synth, the confirmation says so, and just before writing the writer reads the Performance
// Edit Buffer to check that it names the target Bank (hardware::kPerformanceNamesSelectedBank).

#include "nl2x/HardwareAssumptions.h"
#include "nl2x/Library.h"
#include "nl2x/Program.h"
#include "nl2x/Slot.h"
#include "nl2x/TimeMs.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace nl2x {

class Engine;
class SynthConnection;

// A date and time on the user's clock, for backup file names: the engine never reads a clock.
struct WallClockTime {
    int year = 2000;
    int month = 1;
    int day = 1;
    int hour = 0;
    int minute = 0;
    int second = 0;
};

enum class MemoryOperation : std::uint8_t {
    StoreProgram,
    SendBank,
    ReadBank,
    // Reading several Banks into the Librarian (no file, nothing written).
    ReadBanks,
    // Writing a set of locations of one Bank (the Librarian's send).
    SendPrograms,
};

// One location to write: Program `program` (1–99) of the target Bank will hold `value`.
struct ProgramWrite {
    int program = 1;
    Program value;
};

// Told what the memory writer learns about the synth's Banks, whatever the task (the Librarian
// keeps its copy of the synth's Banks with it). Called before the writer's own change callback.
class MemoryWriterListener {
public:
    virtual ~MemoryWriterListener() = default;
    // Program `program` (1–99) of `bank` was read from the synth: a backup, a Bank read or a
    // read-back after writing.
    virtual void programRead(int bank, int program, const Program& value) = 0;
    // A dump storing `value` at Program `program` of `bank` was sent (read back afterwards when
    // Bank reading is verified, through programRead).
    virtual void programWritten(int bank, int program, const Program& value) = 0;
    // The task ended (finished, failed or cancelled): status() says how.
    virtual void taskEnded() = 0;
};

enum class MemoryStage : std::uint8_t {
    Idle,
    // Reading the target Bank from the synth into the backup file; nothing written yet.
    ReadingBackup,
    // status().plan names what will be overwritten; waiting for confirm() or cancel().
    AwaitingConfirmation,
    // Confirmed: reading the Performance Edit Buffer to check which Bank is selected.
    CheckingSelectedBank,
    // Sending Bank dumps, kBankWriteSpacingMs apart.
    Writing,
    // Reading the written locations back (only when Bank reading is verified).
    Verifying,
    // ReadBank, ReadBanks: requesting the Bank's Programs one by one.
    ReadingBank,
    Finished,
    Failed,
};

// What a confirmation is asked for. The GUI shows `question` and `details`; when
// `needsNoBackupConsent`, it then shows `noBackupWarning` as a second, separate confirmation.
struct MemoryWritePlan {
    // Changes every time a plan is offered: confirm() takes only the current one.
    std::uint64_t token = 0;
    MemoryOperation operation = MemoryOperation::StoreProgram;
    // As the synth shows them: Bank 0–3, Programs 1–99.
    int bank = 0;
    int firstProgram = 1;
    int programCount = 0;
    // Every Program (1–99) that will be overwritten, ascending; firstProgram is the first of them.
    std::vector<int> programs;
    // The backup of the whole target Bank, written before the plan was offered; empty when no
    // backup is possible (Bank reading not verified).
    std::optional<std::filesystem::path> backupFile;
    bool needsNoBackupConsent = false;
    // "Overwrite Program 17 in Bank 2 on the synth?"
    std::string question;
    std::string details;
    std::string noBackupWarning;
};

struct MemoryTaskStatus {
    MemoryStage stage = MemoryStage::Idle;
    MemoryOperation operation = MemoryOperation::StoreProgram;
    // ReadBanks: the Bank being read.
    int bank = 0;
    // Progress of the current stage (locations read or written; ReadBanks: of all its Banks).
    int done = 0;
    int total = 0;
    // Set while AwaitingConfirmation.
    std::optional<MemoryWritePlan> plan;
    std::optional<std::filesystem::path> backupFile;
    // ReadBank: the file the Bank was read into, once written.
    std::optional<std::filesystem::path> bankFile;
    // The outcome, or why the plan is offered again, for the GUI.
    std::string message;
};

enum class MemoryRefusal : std::uint8_t {
    None,
    NotConnected,
    RequestsSuspended,
    Busy,
    // Only the RAM Banks 0–3 can be written; reading is limited to them too.
    NotARamBank,
    ProgramOutOfRange,
    SlotHasNoProgram,
    // A Bank send takes a file the Library recognises as a Program Bank.
    NotAProgramBank,
    NoProgramsInFile,
    // A Bank holds 99 Programs.
    TooManyProgramsInFile,
    // The assumed request type for this Bank is the Slot Edit Buffer request (type 14).
    RequestTypeCollides,
    FileExists,
    // A set of Programs to write is empty, or names a location twice.
    NothingToWrite,
    DuplicateProgram,
    // The Library entry is a Percussion Kit or a Performance.
    NotAProgram,
};

std::string memoryRefusalText(MemoryRefusal refusal);

enum class NoBackupConsent : std::uint8_t { NotGiven, Given };

enum class MemoryConfirmResult : std::uint8_t {
    Accepted,
    // No plan is waiting, or the token is not the current plan's: nothing written.
    RefusedNoSuchPlan,
    // The plan has no backup and the "no backup possible" consent was not given.
    RefusedNoBackupConsent,
};

class MemoryWriter {
public:
    // How Banks are read comes from `switches` (bankReadingVerified, bankRequestTypeOfSysExBank1):
    // the constants of HardwareAssumptions.h in the shell, other values in tests of the lock.
    MemoryWriter(const Engine& engine, SynthConnection& connection, const hardware::Switches& switches,
                 std::function<void()> changed);

    bool bankReadingVerified() const { return reading.verified; }

    // At most one listener (the Engine's Librarian); nullptr removes it.
    void setListener(MemoryWriterListener* newListener) { listener = newListener; }

    // Store the Slot's Program (as the engine holds it when confirmed) as Program `program` (1–99)
    // of Bank `bank` (0–3). Backs up that Bank first when Bank reading is verified.
    MemoryRefusal prepareStoreProgram(Slot slot, int bank, int program, const std::filesystem::path& backupFolder,
                                      WallClockTime now, TimeMs timeMs);
    // Store `value` (a Library Program, named `sourceName` in the question) the same way.
    MemoryRefusal prepareStoreProgram(const Program& value, const std::string& sourceName, int bank, int program,
                                      const std::filesystem::path& backupFolder, WallClockTime now, TimeMs timeMs);
    // Write `writes` into Bank `bank` (0–3), one Program dump per location, in ascending order;
    // `sourceName` says in the question where they come from ("the Librarian's Bank 2"). Backs up
    // that Bank first when Bank reading is verified.
    MemoryRefusal prepareProgramWrites(int bank, std::vector<ProgramWrite> writes, const std::string& sourceName,
                                       const std::filesystem::path& backupFolder, WallClockTime now,
                                       TimeMs timeMs);
    // Send the Programs of a Program Bank file, in file order, to Programs 1… of Bank `bank`
    // (0–3); the file's type and spec bytes are ignored and its Percussion Kits are not sent.
    // Backs up that Bank first when Bank reading is verified.
    MemoryRefusal prepareBankSend(const LibraryFile& file, int bank, const std::filesystem::path& backupFolder,
                                  WallClockTime now, TimeMs timeMs);
    // Read Programs 1–99 of Bank `bank` into `file`, replies kept exactly as received. Writes nothing
    // to the synth, so it needs no confirmation; experimental while Bank reading is not verified.
    MemoryRefusal readBank(int bank, const std::filesystem::path& file, ReplaceExisting replace, TimeMs timeMs);
    // Read Programs 1–99 of each Bank in `banks` (0–3), in order, for the listener only: no file,
    // nothing written to the synth, no confirmation.
    MemoryRefusal readBanks(std::vector<int> banks, TimeMs timeMs);

    // The only way anything is written.
    MemoryConfirmResult confirm(std::uint64_t token, NoBackupConsent consent, TimeMs timeMs);
    // Stops the task; while writing, the Programs not sent yet stay as they were.
    void cancel();

    const MemoryTaskStatus& status() const { return current; }
    // A task is under way or waiting for its confirmation.
    bool busy() const;
    // From confirmation to the end of the read-back: the Engine then sends no Edit Buffer dump
    // (Morph Program dumps, Auditions, Performance edits) until the write is over. A Performance
    // dump would set the Bank selected on the synth (its bankselect bytes are received) after the
    // check; the others are held too, so that only the write and its own requests reach the synth.
    bool holdsEditBufferDumps() const;

    // Called by the Engine.
    void advanceTime(TimeMs timeMs);
    // The connection was reset (Engine::connect): whatever was queued is gone.
    void abort(const std::string& reason);
    // A Bank Select or Program Change arrived from the synth: someone selected a sound on it, so the
    // selected Bank may no longer be the one checked. Stops a write under way (the Programs not sent
    // yet stay as they were); the plan of a write not confirmed yet is unaffected, since the Bank
    // is checked after confirming.
    void selectionChangedOnSynth();
    // The Engine sent a Performance dump to the Performance Edit Buffer (project recall, a
    // Performance Audition). Unless the synth applies such dumps, it keeps it without playing it,
    // and a read of the Performance Edit Buffer returns that dump, not the Banks the Slots play
    // (hardware::kPerformanceEditBufferDumpIsApplied). Until a sound is selected on the synth the
    // check of the selected Bank cannot be made: the writer asks the user to select one instead of
    // naming a Bank the synth does not have selected.
    void performanceDumpSent();
    // A Program Change arrived from the synth: a sound was loaded, and the Performance Edit Buffer
    // names what the synth plays again.
    void soundSelectedOnSynth();

private:
    struct Task;

    MemoryRefusal startRefusal(int bank) const;
    std::uint8_t requestTypeOf(int bank) const;
    void begin(MemoryOperation operation, int bank, std::vector<int> locations);
    // Every write: `programs` by index of `locations` (empty for a Slot's Program, taken on
    // confirm); `setUp` adds what only this kind of write needs.
    MemoryRefusal startWrite(MemoryOperation operation, int bank, std::vector<int> locations,
                             std::vector<Program> programs, const std::filesystem::path& backupFolder,
                             WallClockTime now, TimeMs timeMs, const std::function<void(Task&)>& setUp);
    void readLocation(int location, TimeMs timeMs);
    void locationRead(int location, std::optional<std::span<const std::uint8_t>> reply, TimeMs timeMs);
    void nextBankRead(TimeMs timeMs);
    void backupComplete(TimeMs timeMs);
    void readComplete();
    void offerPlan(std::string message);
    // The Performance Edit Buffer holds a dump the Engine sent, so it does not name the selected Bank.
    bool selectedBankUnreadable() const;
    std::string notOnStoredSlot() const;
    static std::string sentDumpReason();
    void checkSelectedBank(TimeMs timeMs);
    void selectedBankChecked(std::optional<std::span<const std::uint8_t>> reply, TimeMs timeMs);
    void startWriting(TimeMs timeMs);
    void writeDue(TimeMs timeMs);
    void verified(TimeMs timeMs);
    void finish(std::string message);
    void fail(std::string message);
    std::string locationsText() const;
    void changed() { onChange(); }
    void ended();

    struct Task {
        MemoryOperation operation = MemoryOperation::StoreProgram;
        int bank = 0;
        // The locations (0–98) written, ascending.
        std::vector<int> locations;
        // A Store of a Slot's Program, taken when confirmed.
        std::optional<Slot> slot;
        // What will be written, by index of `locations`: set on confirm for a Slot's Program, on
        // prepare otherwise.
        std::vector<Program> programs;
        // ReadBanks: the Banks to read, and which of them is being read.
        std::vector<int> banksToRead;
        std::size_t bankIndex = 0;
        std::filesystem::path backupFolder;
        WallClockTime time;
        std::filesystem::path readFile;
        ReplaceExisting replace = ReplaceExisting::No;
        // Where the Programs come from (a Bank file, a Library entry, the Librarian), and how many
        // Percussion Kits of a Bank file are left out.
        std::string sourceName;
        std::size_t skippedKits = 0;
        std::vector<std::vector<std::uint8_t>> replies;
        int written = 0;
        int matches = 0;
        TimeMs nextWriteMs = 0;
        bool verifyStarted = false;
    };

    const Engine& engine;
    SynthConnection& connection;
    struct BankReading {
        bool verified;
        std::uint8_t requestTypeOfSysExBank1;
    };
    BankReading reading;
    bool performanceDumpsApplied;
    // A Performance dump sent by the Engine is in the Performance Edit Buffer (performanceDumpSent).
    bool performanceEditBufferHoldsSentDump = false;
    std::function<void()> onChange;
    MemoryWriterListener* listener = nullptr;
    std::optional<Task> task;
    MemoryTaskStatus current;
    std::uint64_t lastToken = 0;
};

} // namespace nl2x
