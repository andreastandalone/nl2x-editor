#pragma once

// The Library (GLOSSARY.md, spec #1 user stories 48–57): the user's .syx files in a folder on the
// computer. Files are recognised by the sizes of the dumps they hold, never by type bytes
// (research §3); entries are named after the file, numbered inside a Bank file. Saving writes
// standard Edit Buffer dumps that any SysEx tool can send, and never overwrites a file unless the
// caller says the user agreed. Paths come from the caller (CODING_STANDARDS.md).

#include "nl2x/Audition.h"
#include "nl2x/Performance.h"
#include "nl2x/Program.h"
#include "nl2x/Slot.h"
#include "nl2x/SysEx.h"
#include "nl2x/TimeMs.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace nl2x {

class Engine;

enum class LibraryEntryKind : std::uint8_t { Program, PercussionKit, Performance };

// One dump in a Library file, with its header bytes as found in the file.
struct LibraryEntry {
    // The file name without extension for a file holding one dump; "Bank N – P" inside a Bank
    // file (see libraryEntryName).
    std::string name;
    std::variant<ProgramDump, PercussionKitDump, PerformanceDump> dump;

    LibraryEntryKind kind() const { return static_cast<LibraryEntryKind>(dump.index()); }
};

enum class LibraryFileKind : std::uint8_t {
    Program,         // one Program dump (139 bytes)
    PercussionKit,   // one Percussion Kit dump (1063 bytes)
    Performance,     // one Performance dump (715 bytes)
    ProgramBank,     // several Program and/or Percussion Kit dumps (the factory "bankN.syx")
    PerformanceBank, // several Performance dumps (the factory "Perf0.syx")
    Mixed,           // Performances together with Programs or Kits
    Unknown,         // no Nord Lead 2X dump at all, or the file could not be read
};

std::string_view libraryFileKindName(LibraryFileKind kind);

struct LibraryFile {
    std::filesystem::path path;
    // The file name without extension.
    std::string name;
    LibraryFileKind kind = LibraryFileKind::Unknown;
    // In file order.
    std::vector<LibraryEntry> entries;
    // Messages that are not decodable Nord Lead 2X dumps (other devices, other sizes, bad nibbles):
    // skipped, never guessed at.
    std::size_t ignoredMessageCount = 0;
};

// Recognises a file's bytes; `name` is the file name without extension.
LibraryFile recogniseLibraryFile(const std::string& name, std::span<const std::uint8_t> bytes);
// Reads and recognises one file. An unreadable or oversized file comes back as Unknown.
LibraryFile openLibraryFile(const std::filesystem::path& path);
// Every .syx file (any case) under `folder`, subfolders included, hidden ones skipped, sorted by
// path. A missing folder gives an empty Library.
std::vector<LibraryFile> scanLibrary(const std::filesystem::path& folder);

// The name of the `index`-th entry of its kind (0-based) in a file holding several dumps.
// Programs are numbered 1–99 as the synth shows Program numbers, Percussion Kits P0–P9 and
// Performances A0–L9 as the synth names them [OFF-2X p.13, 16], continuing with plain numbers
// past the end. The Bank number N is the number the file name ends with ("bank2" → "Bank 2 – 17");
// a file name that ends with no number is used itself ("My sounds – 17"). Neither the type byte
// nor the spec byte is used: both are unreliable as a location (research §3).
std::string libraryEntryName(const std::string& fileName, LibraryEntryKind kind, std::size_t index);

// The file's dumps written back as they were read: the same bytes as the file, minus anything
// that was ignored.
std::vector<std::uint8_t> toSysEx(const LibraryFile& file);

// The entry as a single Edit Buffer dump, which sends it to an Edit Buffer and never into a Bank:
// a dump that already is an Edit Buffer dump keeps its header; a Bank dump gets the Edit Buffer
// header of Slot A (Program: type 0, spec 0; Kit: type 0, spec 16; Performance: type 30, spec 0),
// keeping its Device ID byte. Data bytes are never changed.
std::vector<std::uint8_t> editBufferDumpOf(const LibraryEntry& entry);

// Auditions an entry: a Program or Kit on `slot`, a Performance on the Performance Edit Buffer.
AuditionResult audition(Engine& engine, const LibraryEntry& entry, Slot slot, TimeMs timeMs);

// The file for "Save Slot as .syx": a Program dump into the Edit Buffer of the Slot it came from
// (type 0, spec = Slot), so that sending it with any tool plays it on that Slot again. Empty unless
// the Slot plays a Program known to the engine.
std::optional<std::vector<std::uint8_t>> slotProgramFile(const Engine& engine, Slot slot);
// The file for "Save Performance as .syx": a Performance Edit Buffer dump (type 30, spec 0) of the
// Performance as last read and edited, with the Programs of Slots playing a Program taken from
// their Edit Buffers, as the engine sends it. Empty until the Performance has been read.
std::optional<std::vector<std::uint8_t>> performanceFile(const Engine& engine);

// A file name made from `name`: characters that macOS or other systems refuse in file names are
// replaced, and ".syx" is added. Empty names become "Untitled".
std::string libraryFileName(const std::string& name);
// `folder`/libraryFileName(name), or with " 2", " 3", … added before the extension until no such
// file exists.
std::filesystem::path uniqueLibraryPath(const std::filesystem::path& folder, const std::string& name);

enum class LibrarySaveResult : std::uint8_t {
    Saved,
    // Nothing written: the file exists and the caller did not allow replacing it.
    AlreadyExists,
    // Nothing written: the folder could not be created, or the file could not be written.
    Failed,
};

enum class ReplaceExisting : std::uint8_t { No, Yes };

// Writes `bytes` to `path`, creating its folder when missing. Replaces an existing file only with
// ReplaceExisting::Yes, which the caller passes only after the user agreed.
LibrarySaveResult saveLibraryFile(const std::filesystem::path& path, std::span<const std::uint8_t> bytes,
                                  ReplaceExisting replace = ReplaceExisting::No);

} // namespace nl2x
