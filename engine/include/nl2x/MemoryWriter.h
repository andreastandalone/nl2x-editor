#pragma once

// The memory writer (spec #1, user stories 58–63): stores a Slot's or a Library Program into a Bank
// location, sends a whole Bank file or a set of Programs (the Librarian's pending locations, #44)
// to the synth, reads a whole Bank into a .syx file and reads Banks for the Librarian.
//
// Nothing is ever written without a confirmation: prepareStoreProgram() and prepareBankSend()
// only read (the backup) and then hold a MemoryWritePlan naming exactly what will be overwritten;
// only confirm() with that plan's token writes. Every write goes one Program dump per location, so
// only the locations named in the plan change. The order is fixed: backup → confirmation → write →
// read-back. While reading Banks is not verified on hardware
// (hardware::kBankReadingVerified, the lock), no backup can be made and every plan needs the
// user's separate "no backup possible" consent.
//
// The synth stores a received Bank dump in the Bank of the lit Slot (the active Slot, whose button
// is lit on the panel), whatever the dump's type byte says (hardware::kFirstProgramBankDumpType).
// So before the plan is offered, and again after Overwrite, the writer reads the Performance Edit
// Buffer (type 40), which names the lit Slot and each Slot's Bank and Program
// (hardware::kPerformanceNamesSelectedBank). Then:
// - the lit Slot is on the target Bank already: it writes;
// - otherwise, when it can (BankSetting::SwitchedByEditor): it reads the lit Slot's Edit Buffer,
//   sends Bank Select and Program Change on the lit Slot's channel so that the lit Slot shows the
//   target Bank, checks that with another type-40 read, writes, then selects the lit Slot's own
//   Bank and Program again and sends its Edit Buffer back (unsaved edits included);
// - when it cannot (the lit Slot's channel is off, shared or the Global Channel, the lit Slot plays
//   a Percussion Kit, or the type-40 read is not to be trusted because the editor sent the synth a
//   Performance dump), the confirmation asks the user, as numbered steps placed first, to set the
//   lit Slot to the target Bank on the synth (BankSetting::ByHand). After Overwrite the writer
//   then checks the lit Slot's Bank when it can, and writes nothing if it is not the target Bank.
// When Bank reading is verified, the written locations are read back from the target Bank
// afterwards; if they do not hold what was sent, the task fails and says the synth probably stored
// them in another Bank.

#include "nl2x/HardwareAssumptions.h"
#include "nl2x/Library.h"
#include "nl2x/MidiOutput.h"
#include "nl2x/Performance.h"
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
    // The read-back of Program `program` of `bank` did not match the dump sent there (programRead
    // follows with what the location holds): the dump was not stored there.
    virtual void programNotStored(int bank, int program) = 0;
    // The task ended (finished, failed or cancelled): status() says how.
    virtual void taskEnded() = 0;
};

enum class MemoryStage : std::uint8_t {
    Idle,
    // Reading the target Bank from the synth into the backup file; nothing written yet.
    ReadingBackup,
    // Reading the Performance Edit Buffer to learn which Slot is lit and which Bank it is on: before
    // the plan is offered, and again after Overwrite.
    ReadingLitSlot,
    // status().plan names what will be overwritten; waiting for confirm() or cancel().
    AwaitingConfirmation,
    // Saving the lit Slot's Edit Buffer, then switching the lit Slot to the target Bank (Bank Select
    // and Program Change on its channel) and checking that it shows it.
    SwitchingBank,
    // Sending Bank dumps, kBankWriteSpacingMs apart.
    Writing,
    // Putting the lit Slot back: its own Bank and Program, then its saved Edit Buffer.
    RestoringSlot,
    // Reading the written locations back (only when Bank reading is verified), then, after a switch,
    // the lit Slot's Edit Buffer and Bank, to check that it is back as it was.
    Verifying,
    // ReadBank, ReadBanks: requesting the Bank's Programs one by one.
    ReadingBank,
    Finished,
    Failed,
};

// How the target Bank comes to be the lit Slot's Bank, where the synth stores what it receives.
enum class BankSetting : std::uint8_t {
    // The lit Slot is on the target Bank already.
    AlreadyThere,
    // The editor switches the lit Slot to the target Bank for the write and puts it back afterwards.
    SwitchedByEditor,
    // The user sets the lit Slot to the target Bank on the synth before confirming.
    ByHand,
};

// What a confirmation is asked for. The GUI shows `question`, then `panelSteps` first and
// `details` after them; when
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
    BankSetting bankSetting = BankSetting::ByHand;
    // What happens on the synth, shown first: with BankSetting::ByHand, what to do on its panel
    // before confirming, as numbered lines; otherwise what the editor will do with the lit Slot.
    std::string panelSteps;
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
    // The outcome, for the GUI.
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
    // Bank Select and Program Change for the lit Slot go straight to `output`.
    MemoryWriter(const Engine& engine, SynthConnection& connection, MidiOutput& output,
                 const hardware::Switches& switches, std::function<void()> changed);

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
    // (Morph Program dumps, Auditions, Performance edits, project recall) until the write is over,
    // so that only the write and its own requests reach the synth, and the lit Slot's Edit Buffer
    // is not changed while it is switched to another Bank.
    bool holdsEditBufferDumps() const;

    // Called by the Engine.
    void advanceTime(TimeMs timeMs);
    // The connection was reset (Engine::connect): whatever was queued is gone.
    void abort(const std::string& reason);
    // A Bank Select or Program Change arrived from the synth: someone selected a sound on it, so the
    // lit Slot's Bank may no longer be the target Bank. Stops a write under way (the Programs not
    // sent yet stay as they were; a switched lit Slot is not put back, so as not to undo what the
    // user selected); the plan of a write not confirmed yet is unaffected, since the lit Slot is
    // read again after Overwrite.
    void selectionChangedOnSynth();

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

    // The lit Slot as a type-40 read names it: which Slot, and the Bank (0–9) and Program number
    // (0–98 = Program 1–99, 99–108 = Kit P0–P9) it was loaded from.
    struct LitSlot {
        Slot slot = Slot::A;
        int bank = 0;
        int programNumber = 0;
    };
    // How the target Bank can be set, as decided from a type-40 read.
    struct BankDecision {
        BankSetting setting = BankSetting::ByHand;
        // Empty when the read is missing or not to be trusted.
        std::optional<LitSlot> lit;
        // SwitchedByEditor: the lit Slot's channel.
        MidiChannel channel = 0;
        // ByHand: why the editor cannot switch the lit Slot itself.
        std::string reason;
        // The read was checked against the lit Slot's Edit Buffer (not a Performance the synth kept).
        bool checked = false;
    };
    using PerformanceRead = std::function<void(const std::optional<Performance>&, TimeMs)>;
    void readPerformance(TimeMs timeMs, PerformanceRead then);
    void readLitSlot(TimeMs timeMs);
    BankDecision decide(const std::optional<Performance>& performance) const;
    void litSlotRead(const std::optional<Performance>& performance, TimeMs timeMs);
    void offerPlan();
    std::string panelStepsText() const;
    void startSwitching(TimeMs timeMs);
    void sendSelection(int bank, int programNumber);
    void switchChecked(const std::optional<Performance>& performance, TimeMs timeMs);
    void startRestoring(std::optional<std::string> failure, TimeMs timeMs);
    void stepDue(TimeMs timeMs);
    void startWriting(TimeMs timeMs);
    void writeDue(TimeMs timeMs);
    void startVerifying(TimeMs timeMs);
    void verified(TimeMs timeMs);
    void checkRestoredSlot(TimeMs timeMs);
    void finishWrite(const std::string& slotNote);
    std::string litSlotName() const;
    std::string litSlotOriginText() const;
    std::string notPutBackText() const;
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
        // The Programs (1–99) whose read-back differs from what was sent.
        std::vector<int> notStored;
        TimeMs nextWriteMs = 0;
        bool verifyStarted = false;
        // Overwrite was clicked: from then on until the end, Edit Buffer dumps are held.
        bool confirmed = false;
        // The latest decision about the target Bank (before the plan, then after Overwrite).
        BankDecision decision;
        // SwitchedByEditor: the lit Slot as it was, and its Edit Buffer, saved before the switch.
        std::optional<LitSlot> lit;
        MidiChannel litChannel = 0;
        std::optional<Program> savedEditBuffer;
        // Bank Select and Program Change went out to switch the lit Slot; and to put it back.
        bool switched = false;
        bool restored = false;
        // What is still to be sent or read to switch or restore the lit Slot, and when.
        enum class Step : std::uint8_t { None, SendSwitch, CheckSwitch, SendRestore, SendEditBuffer };
        Step step = Step::None;
        TimeMs stepDueMs = 0;
        // Set when the task ends once the lit Slot is back: the failure message, or a cancellation.
        std::optional<std::string> endAfterRestore;
        bool cancelledAfterRestore = false;
    };

    const Engine& engine;
    SynthConnection& connection;
    MidiOutput& output;
    struct BankReading {
        bool verified;
        std::uint8_t requestTypeOfSysExBank1;
    };
    BankReading reading;
    std::function<void()> onChange;
    MemoryWriterListener* listener = nullptr;
    std::optional<Task> task;
    MemoryTaskStatus current;
    std::uint64_t lastToken = 0;
};

} // namespace nl2x
