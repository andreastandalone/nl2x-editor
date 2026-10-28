#pragma once

// The memory writer (spec #1, user stories 58–63): stores a Slot's Program into a Bank location,
// sends a whole Bank file to the synth and reads a whole Bank into a .syx file.
//
// Nothing is ever written without a confirmation: prepareStoreProgram() and prepareBankSend()
// only read (the backup) and then hold a MemoryWritePlan naming exactly what will be overwritten;
// only confirm() with that plan's token writes. The order is fixed: backup → confirmation → check
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

enum class MemoryOperation : std::uint8_t { StoreProgram, SendBank, ReadBank };

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
    // ReadBank: requesting the Bank's Programs one by one.
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
    int bank = 0;
    // Progress of the current stage (locations read or written).
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

    // Store the Slot's Program (as the engine holds it when confirmed) as Program `program` (1–99)
    // of Bank `bank` (0–3). Backs up that Bank first when Bank reading is verified.
    MemoryRefusal prepareStoreProgram(Slot slot, int bank, int program, const std::filesystem::path& backupFolder,
                                      WallClockTime now, TimeMs timeMs);
    // Send the Programs of a Program Bank file, in file order, to Programs 1… of Bank `bank`
    // (0–3); the file's type and spec bytes are ignored and its Percussion Kits are not sent.
    // Backs up that Bank first when Bank reading is verified.
    MemoryRefusal prepareBankSend(const LibraryFile& file, int bank, const std::filesystem::path& backupFolder,
                                  WallClockTime now, TimeMs timeMs);
    // Read Programs 1–99 of Bank `bank` into `file`, replies kept exactly as received. Writes nothing
    // to the synth, so it needs no confirmation; experimental while Bank reading is not verified.
    MemoryRefusal readBank(int bank, const std::filesystem::path& file, ReplaceExisting replace, TimeMs timeMs);

    // The only way anything is written.
    MemoryConfirmResult confirm(std::uint64_t token, NoBackupConsent consent, TimeMs timeMs);
    // Stops the task; while writing, the Programs not sent yet stay as they were.
    void cancel();

    const MemoryTaskStatus& status() const { return current; }
    // A task is under way or waiting for its confirmation.
    bool busy() const;

    // Called by the Engine.
    void advanceTime(TimeMs timeMs);
    // The connection was reset (Engine::connect): whatever was queued is gone.
    void abort(const std::string& reason);

private:
    MemoryRefusal startRefusal(int bank) const;
    std::uint8_t requestTypeOf(int bank) const;
    void begin(MemoryOperation operation, int bank, int firstLocation, int count);
    void readLocation(int location, TimeMs timeMs);
    void locationRead(int location, std::optional<std::span<const std::uint8_t>> reply, TimeMs timeMs);
    void backupComplete(TimeMs timeMs);
    void readComplete();
    void offerPlan(std::string message);
    void checkSelectedBank(TimeMs timeMs);
    void selectedBankChecked(std::optional<std::span<const std::uint8_t>> reply, TimeMs timeMs);
    void startWriting(TimeMs timeMs);
    void writeDue(TimeMs timeMs);
    void verified(TimeMs timeMs);
    void finish(std::string message);
    void fail(std::string message);
    std::string locationsText() const;
    void changed() { onChange(); }

    struct Task {
        MemoryOperation operation = MemoryOperation::StoreProgram;
        int bank = 0;
        int firstLocation = 0;
        int count = 0;
        Slot slot = Slot::A;
        // What will be written, set on confirm for a Store, on prepare for a Bank send.
        std::vector<Program> programs;
        std::filesystem::path backupFolder;
        WallClockTime time;
        std::filesystem::path readFile;
        ReplaceExisting replace = ReplaceExisting::No;
        // A Bank send: the file's name and how many of its Percussion Kits are left out.
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
    std::function<void()> onChange;
    std::optional<Task> task;
    MemoryTaskStatus current;
    std::uint64_t lastToken = 0;
};

} // namespace nl2x
