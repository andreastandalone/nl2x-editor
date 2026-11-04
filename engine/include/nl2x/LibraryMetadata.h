#pragma once

// The user's own information about Library sounds (#39): a name, a category and a favourite flag.
// Nord Lead 2X dumps hold no names (research §3), so this is user data kept beside the .syx files,
// in one JSON file in the Library folder.
//
// The information belongs to a sound, not to its bytes or to where it is. Each tagged sound has a
// stable id in the file and its latest known parameter values (its fingerprint). A Library entry,
// a Slot or (later) a Bank location only shows whichever sound is there now:
// - an entry whose values equal a sound's fingerprint is that sound;
// - otherwise, an entry that differs from one sound in only a few values (kSimilarSoundMaxDifferences)
//   is that sound edited on the synth panel, unless the sound itself is still in the Library with
//   its exact values (then the entry is a variation of it, not the sound);
// - where the editor knows a sound's history (auditioned from the Library, edited in the editor,
//   saved again: SoundLineage), the sound's fingerprint follows the edits.
//
// The file is never overwritten when it could not be read: the user fixes or removes it by hand,
// and nothing he typed is lost silently. Every write goes to a temporary file that then replaces
// the old one (which is kept as library.json.bak), so a crash never leaves half a file. Every change
// reads the file again first, so two editor windows open on the same folder keep each other's
// changes.
//
// Auto-tag writes many suggested categories at once (autoAssign). Each is marked auto-assigned in
// the file ("autoAssigned": true), so that Undo auto-tag (removeAutoAssigned) takes back exactly
// those the user has not touched since, and so that learned suggestions (LearnedCategories) learn
// only from the user's own categories. The mark is an optional field of format version 1: an
// older editor reads the file and ignores it (if it rewrites the file, the mark is lost and the
// category stays as if set by hand, which loses nothing the user typed).

#include "nl2x/Library.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace nl2x {

// The categories offered by default. The file stores the category as text, so a category not in
// this list (added here later, or written by hand) is kept and offered too.
inline constexpr std::array<std::string_view, 8> kLibraryCategories {
    "Bass", "Lead", "Pad", "Keys", "Brass/Strings", "FX", "Percussion", "Other",
};

// The file in the Library folder that holds the information.
inline constexpr std::string_view kLibraryMetadataFileName = "library.json";

// At most this many parameter bytes may differ for an entry to count as an edited version of a
// tagged sound. Turning a few knobs on the synth changes one to three values. Different sounds
// are often much closer than one would think: among the 394 distinct factory Programs, 45 (11 %)
// have another factory Program within 4 values and 27 (7 %) within 3 (measured 2026-11-02 on the
// factory Banks, which are not in the repository). 4 keeps a typical panel edit and, with the
// rule that a sound still present unchanged is never matched by similarity, keeps factory
// neighbours apart in a Library holding the factory Banks.
inline constexpr int kSimilarSoundMaxDifferences = 4;

// The parameter values of a sound, without header bytes (Device ID, type, spec): the same sound in
// a Bank file and in an Edit Buffer dump has the same fingerprint.
struct SoundFingerprint {
    LibraryEntryKind kind = LibraryEntryKind::Program;
    // Program: 66 bytes; Percussion Kit: its 8 Programs; Performance: its 354 bytes.
    std::vector<std::uint8_t> values;

    static SoundFingerprint of(const LibraryEntry& entry);
    static SoundFingerprint of(const Program& program);
    static SoundFingerprint of(const Performance& performance);

    friend bool operator==(const SoundFingerprint&, const SoundFingerprint&) = default;
};

// The number of values that differ; -1 when the kinds or sizes differ.
int soundDifferences(const SoundFingerprint& a, const SoundFingerprint& b);

struct SoundInfo {
    // Empty: the entry keeps the name the Library gives it (LibraryEntry::name).
    std::string name;
    // Empty: no category.
    std::string category;
    bool favourite = false;
    // The category was written by Auto-tag and not touched by the user since. Only autoAssign()
    // sets it; set() keeps it only while the category stays the same (see set()).
    bool autoAssigned = false;

    bool isEmpty() const { return name.empty() && category.empty() && ! favourite; }
    friend bool operator==(const SoundInfo&, const SoundInfo&) = default;
};

// The tagged sound an entry is.
struct SoundMatch {
    std::string soundId;
    // 0 for an exact match.
    int differences = 0;
};

enum class LibraryMetadataStatus : std::uint8_t {
    // The file was read (or there is none yet, which is an empty Library): changes can be saved.
    Ready,
    // The file exists but is not valid JSON in our format, or it was written by a newer version
    // of the editor. It is shown as empty and never overwritten.
    Unreadable,
};

enum class MetadataSaveResult : std::uint8_t {
    Saved,
    // Nothing written: the file on disk could not be read (LibraryMetadataStatus::Unreadable).
    RefusedUnreadable,
    // Nothing written: the folder or the file could not be written.
    Failed,
};

// What the Library shows: every entry, or only those of one category and/or the favourites.
struct LibraryFilter {
    // Empty: any category.
    std::string category;
    bool favouritesOnly = false;

    bool isActive() const { return ! category.empty() || favouritesOnly; }
    bool matches(const SoundInfo& info) const;
};

class LibraryMetadata {
public:
    // Reads `folder`/library.json. A missing file or folder gives an empty, Ready metadata.
    static LibraryMetadata load(const std::filesystem::path& folder);

    LibraryMetadataStatus status() const { return currentStatus; }
    std::filesystem::path filePath() const;

    // The sounds now in the Library (backups left out), for the rule that a sound still present
    // with its exact values is never matched by similarity. Call it after every scan.
    void setLibraryContents(const std::vector<LibraryFile>& files);

    // The tagged sound `fingerprint` is: an exact match first, then the single closest sound within
    // kSimilarSoundMaxDifferences (none when two are equally close).
    std::optional<SoundMatch> match(const SoundFingerprint& fingerprint) const;
    // The information shown for an entry or a sound; empty when it matches no tagged sound.
    SoundInfo info(const SoundFingerprint& fingerprint) const;
    SoundInfo info(const LibraryEntry& entry) const { return info(SoundFingerprint::of(entry)); }
    // The custom name, or the name the Library gives the entry.
    std::string displayName(const LibraryEntry& entry) const;

    // Stores `info` for the sound `fingerprint` matches and saves at once: the file is read again,
    // the change applied to what is on disk, and the result written. A sound matched by similarity
    // takes `fingerprint` as its latest values; a sound matched by nothing gets a new id; an empty
    // `info` removes the sound. On RefusedUnreadable or Failed nothing changes, here or on disk.
    MetadataSaveResult set(const SoundFingerprint& fingerprint, const SoundInfo& info);
    MetadataSaveResult set(const LibraryEntry& entry, const SoundInfo& info)
    {
        return set(SoundFingerprint::of(entry), info);
    }
    // set() is the user's hand: it never marks a category auto-assigned. It keeps the mark only
    // when `info` keeps both the category stored now and the mark; picking a category, even the
    // same one with `autoAssigned` false, makes it the user's own.

    struct AutoAssignment {
        SoundFingerprint fingerprint;
        std::string category;
    };
    // Auto-tag: gives each sound its category, marked auto-assigned, unless the sound has a
    // category on disk by now (set in another window). One read and one write for all of them;
    // on RefusedUnreadable or Failed nothing changes.
    MetadataSaveResult autoAssign(const std::vector<AutoAssignment>& assignments);
    // Undo auto-tag: removes every auto-assigned category; a sound left with no information is
    // removed from the file.
    MetadataSaveResult removeAutoAssigned();
    // The sounds whose category is auto-assigned.
    int autoAssignedCount() const;

    // The sound `soundId` was edited in the editor into `edited` (and saved): its fingerprint
    // becomes `edited`. Nothing happens when the sound is gone or another sound already has
    // exactly these values.
    MetadataSaveResult followEdit(const std::string& soundId, const SoundFingerprint& edited);

    // kLibraryCategories followed by any other category found in the file, sorted.
    std::vector<std::string> categoryChoices() const;

    struct Sound {
        SoundFingerprint fingerprint;
        SoundInfo info;
    };
    // Every tagged sound, by id.
    const std::map<std::string, Sound>& taggedSounds() const { return sounds; }

private:
    MetadataSaveResult rewrite(const std::map<std::string, Sound>& newSounds);
    void reindex();

    std::filesystem::path folder;
    LibraryMetadataStatus currentStatus = LibraryMetadataStatus::Ready;
    // By id.
    std::map<std::string, Sound> sounds;
    // Fingerprint (kind and values as text) → id.
    std::unordered_map<std::string, std::string> exactIndex;
    std::unordered_set<std::string> presentInLibrary;
};

// Which tagged sound each place in the editor holds as far as the editor knows its history: a
// Slot's Program after a Library Program was auditioned on it, the Performance after a Library
// Performance was sent. Edits made in the editor keep the link; anything that replaces the sound
// at once (a jump of more than kLineageMaxStepDifferences values between two observations, as when
// another Program is selected on the synth) breaks it. Places are named by the caller ("Slot A",
// "Performance"; Bank locations later).
class SoundLineage {
public:
    static constexpr int kLineageMaxStepDifferences = 8;

    void loaded(const std::string& place, const std::string& soundId, const SoundFingerprint& fingerprint);
    // The place now holds `current` (empty: nothing known).
    void observe(const std::string& place, const std::optional<SoundFingerprint>& current);
    void forget(const std::string& place);
    std::optional<std::string> soundAt(const std::string& place) const;

private:
    struct Link {
        std::string soundId;
        SoundFingerprint last;
    };
    std::map<std::string, Link> links;
};

// A Library entry with the file it is in.
struct LibraryEntryRef {
    const LibraryFile* file = nullptr;
    const LibraryEntry* entry = nullptr;
};

// The entries of `files` that `filter` lets through, in file order, backups left out. The
// references point into `files`.
std::vector<LibraryEntryRef> filterLibrary(const std::vector<LibraryFile>& files, const LibraryMetadata& metadata,
                                           const LibraryFilter& filter);

// Entries grouped by the subfolders of their files, as groupLibrary groups files (#44).
struct LibraryEntryGroup {
    // The folder's name; empty for the Library folder itself.
    std::string name;
    std::vector<LibraryEntryGroup> groups;
    std::vector<LibraryEntryRef> entries;
};

// The entries (filterLibrary's, in its order) in the groups of their files' folders: a group with
// no entry in or below it does not appear. The result points where `entries` point.
LibraryEntryGroup groupEntries(const std::vector<LibraryEntryRef>& entries);

} // namespace nl2x
