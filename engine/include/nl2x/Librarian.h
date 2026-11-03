#pragma once

// The Librarian (#44, GLOSSARY.md): the synth's four RAM Program Banks as the editor last read
// them, and a Composed Bank for each, which the user fills from the Library and sends.
//
// For the GUI (the Librarian tab):
// - Synth Banks: readSynthBanks() reads Banks 0–3 (396 Program requests, nothing written) and
//   keeps them while the editor is open; calling it again is "Read again". synthBanksStatus() gives
//   the progress, synthProgram() what the synth holds. Every Bank read made for any other reason
//   (the backup before a write, "Read Bank into file") and every write keep these copies current,
//   so after a send they show what was written.
// - Composed Banks: program() is what a location will hold once sent: its pending change, else the
//   synth's Program. put() and copy() change a location (it becomes pending, "to send"); undo()
//   drops a location's change; replaceWithFile() makes every Program of a Bank file a change (a
//   "whole Bank", sent whole); saveBankFile() writes the Composed Bank as a Library Bank file.
// - Writing: prepareSend() and prepareStore() start a MemoryWriter plan (MemoryWriter.h): the
//   Bank is backed up into the backup folder, the plan waits for the user's confirmation, the Bank
//   selected on the synth is checked, then only the pending locations are written, one Program
//   dump each. The confirmation, its token, the no-backup consent, cancel() and the progress are
//   the memory writer's, exactly as for the other memory tasks: the GUI shows
//   Engine::memoryWriter().status() and calls its confirm() and cancel().
// - Names and tags: soundInfo() looks a location's Program up in the Library metadata, by
//   fingerprint (an exact match first, then LibraryMetadata's similarity rule).
//
// Locations are numbered as the synth shows them: Bank 0–3, Program 1–99. Phase 1 handles Programs
// only: Percussion Kits and Performances wait for #33 and #34.

#include "nl2x/Library.h"
#include "nl2x/LibraryMetadata.h"
#include "nl2x/MemoryWriter.h"
#include "nl2x/Program.h"
#include "nl2x/TimeMs.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace nl2x {

class Engine;

// A Program location in a RAM Bank, as the synth shows it: Bank 0–3, Program 1–99.
struct BankLocation {
    int bank = 0;
    int program = 1;

    friend bool operator==(const BankLocation&, const BankLocation&) = default;
};

enum class SynthBanksStage : std::uint8_t {
    // Never read since the editor opened.
    NotRead,
    Reading,
    // The last read went through all four Banks.
    Read,
    // The last read stopped (no answer, cancelled, connection lost); `message` says why. The
    // Programs read before it stopped are kept.
    Failed,
};

struct SynthBanksStatus {
    SynthBanksStage stage = SynthBanksStage::NotRead;
    // Locations read so far in the current read, of `total` (396).
    int done = 0;
    int total = 0;
    std::string message;
};

enum class LibrarianRefusal : std::uint8_t {
    None,
    NotARamBank,
    ProgramOutOfRange,
    // Nothing is known about the Bank yet: read the synth's Banks or load a Bank file first (a
    // Composed Bank never starts empty).
    BankNotRead,
    // The source location holds no known Program.
    NoProgramThere,
    // A Library entry that is a Percussion Kit or a Performance (Programs only in phase 1).
    NotAProgram,
    // replaceWithFile() takes a Program Bank file.
    NotAProgramBank,
    NoProgramsInFile,
    TooManyProgramsInFile,
    // saveBankFile(): a location is unknown while a later one is known, so the file's order would
    // not match the Bank's locations.
    BankIncomplete,
    FileExists,
    SaveFailed,
};

std::string librarianRefusalText(LibrarianRefusal refusal);

class Librarian final : public MemoryWriterListener {
public:
    // `changed` is called whenever anything the GUI shows here changes.
    Librarian(const Engine& engine, MemoryWriter& memory, std::function<void()> changed);

    // --- The synth's Banks.

    // Reads Programs 1–99 of Banks 0–3 from the synth; nothing is written. Refused (with the memory
    // writer's reason) while not connected or while another memory task is under way.
    MemoryRefusal readSynthBanks(TimeMs timeMs);
    SynthBanksStatus synthBanksStatus() const;
    // All 99 Programs of the Bank are known (read whole at least once).
    bool synthBankRead(int bank) const;
    // What the synth holds at the location as last read or written; empty when unknown.
    std::optional<Program> synthProgram(BankLocation location) const;

    // --- Composed Banks.

    // What the location will hold after the next send: its pending change, else the synth's
    // Program; empty when unknown.
    std::optional<Program> program(BankLocation location) const;
    bool isPending(BankLocation location) const;
    // The pending Programs of a Bank (1–99), ascending.
    std::vector<int> pendingPrograms(int bank) const;
    // The name of the Bank file the Composed Bank was replaced with (sent as a whole Bank), until
    // all of it has been sent or discarded.
    std::optional<std::string> wholeBankSource(int bank) const;

    // The location will hold `program` (from a Library entry, or another location). Refused when
    // nothing is known about the target Bank yet.
    LibrarianRefusal put(BankLocation location, const Program& program);
    LibrarianRefusal put(BankLocation location, const LibraryEntry& entry);
    LibrarianRefusal copy(BankLocation from, BankLocation to);
    // Drops the location's pending change: it keeps what the synth holds.
    void undo(BankLocation location);
    // Drops every pending change of the Bank.
    void discardChanges(int bank);
    // Every Program of a Program Bank file, in file order, becomes the pending change of Programs
    // 1… of the Bank (its Percussion Kits and header bytes are ignored); other changes are dropped.
    LibrarianRefusal replaceWithFile(int bank, const LibraryFile& file);
    // Writes the Composed Bank as a Program Bank file the Library reads: one Bank Program dump per
    // location in order, typed as the synth's own replies for that Bank are (type = the manual's
    // SysEx Bank, spec = location, Device ID = the Global Channel), like "Read Bank into file".
    // Locations after the last known one are left out; a gap before it is refused.
    LibrarianRefusal saveBankFile(int bank, const std::filesystem::path& file,
                                  ReplaceExisting replace = ReplaceExisting::No) const;

    // --- Writing to the synth (MemoryWriter plans).

    // Writes the Bank's pending locations, each as a single Program dump, after a backup of the
    // whole Bank. Locations written leave the pending list (also when the write stops part way).
    MemoryRefusal prepareSend(int bank, const std::filesystem::path& backupFolder, WallClockTime now,
                              TimeMs timeMs);
    // Stores a Library Program into one location, after a backup of its Bank; the Composed Bank is
    // untouched (a pending change there stays pending unless it is that same Program).
    MemoryRefusal prepareStore(const LibraryEntry& entry, BankLocation location,
                               const std::filesystem::path& backupFolder, WallClockTime now, TimeMs timeMs);

    // --- Names and tags.

    // The tagged sound the location's Program (as program() gives it) is, by its fingerprint.
    std::optional<SoundMatch> soundMatch(const LibraryMetadata& metadata, BankLocation location) const;
    // Its name, category and favourite flag; empty when it matches no tagged sound or is unknown.
    SoundInfo soundInfo(const LibraryMetadata& metadata, BankLocation location) const;

    // MemoryWriterListener: called by the memory writer only.
    void programRead(int bank, int program, const Program& value) override;
    void programWritten(int bank, int program, const Program& value) override;
    void taskEnded() override;

private:
    static constexpr int kBankCount = hardware::kLastRamProgramBank - hardware::kFirstRamProgramBank + 1;
    static constexpr int kProgramsPerBank = hardware::kProgramsPerRamBank;

    struct Bank {
        std::array<std::optional<Program>, kProgramsPerBank> synth;
        // Location (0–98) → the Program to send there.
        std::map<int, Program> changes;
        std::optional<std::string> wholeBankSource;
    };

    static bool valid(BankLocation location);
    Bank& bankAt(int bank) { return banks[static_cast<std::size_t>(bank - hardware::kFirstRamProgramBank)]; }
    const Bank& bankAt(int bank) const
    {
        return banks[static_cast<std::size_t>(bank - hardware::kFirstRamProgramBank)];
    }
    bool anythingKnown(int bank) const;
    void changed() { onChange(); }

    const Engine& engine;
    MemoryWriter& memory;
    std::function<void()> onChange;
    std::array<Bank, kBankCount> banks;
    SynthBanksStage readStage = SynthBanksStage::NotRead;
    std::string readMessage;
};

} // namespace nl2x
