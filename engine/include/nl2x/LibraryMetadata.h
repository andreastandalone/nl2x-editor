#pragma once

// The user's own information about Library sounds (#39): a name, any number of tags and a favourite
// flag.
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
// Auto-tag writes many suggested tags at once (autoAssign). Each is marked auto-assigned in the
// file (listed in "autoTags" besides "tags"), so that Undo auto-tag (removeAutoAssigned) takes back
// exactly those the user has not confirmed since, and so that learned suggestions (LearnedTags)
// learn only from the user's own tags.
//
// Format version 2 holds a list of tags per sound. Version 1 (one "category" per sound, and an
// "autoAssigned" flag for it) is still read: the category becomes the sound's only tag, auto-assigned
// if it was. The first save over a version 1 file also keeps it as library.json.v1.bak, once. An
// editor older than version 2 finds a version 2 file unreadable and so never overwrites it.

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

// The tags offered by default: what a sound is first, then how it plays. The file stores tags as
// text, so a tag not in this list (typed by the user, or a category of format version 1 such as
// "Brass/Strings" or "Other") is kept and offered too.
inline constexpr std::array<std::string_view, 18> kLibraryTags {
    "Bass", "Lead", "Pad", "Keys", "Organ", "Bell", "Pluck", "Strings", "Brass",
    "Synth", "Poly", "Mono", "Arp", "Rhythmic", "Sweep", "Ambient", "FX", "Percussion",
};

// A tag as stored: spaces at either end removed. Empty: no tag.
std::string normalisedTag(std::string_view tag);

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
    // In the order they were added; no duplicates, none empty.
    std::vector<std::string> tags {};
    bool favourite = false;
    // The tags written by Auto-tag and not confirmed by the user since: some of `tags`. Only
    // autoAssign() adds to it; set() keeps a tag here only while it was here before (see set()).
    std::vector<std::string> autoTags {};

    bool hasTag(std::string_view tag) const;
    bool isAutoTag(std::string_view tag) const;
    // The tags the user set or confirmed himself.
    std::vector<std::string> ownTags() const;
    // Adds the normalised `tag` at the end, as the user's own; false when it is empty or already
    // there.
    bool addTag(std::string_view tag);
    // Removes `tag` (and its auto-assigned mark); false when it is not there.
    bool removeTag(std::string_view tag);
    // Puts `to` in the place of `from`, as the user's own; when `to` is already there, `from` is
    // only removed. False when `from` is not there or `to` is empty.
    bool replaceTag(std::string_view from, std::string_view to);
    // Makes an auto-assigned tag the user's own.
    void confirmTag(std::string_view tag);

    bool isEmpty() const { return name.empty() && tags.empty() && ! favourite; }
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

// What the Library shows: every entry, or only those with one tag and/or the favourites.
struct LibraryFilter {
    // Empty: any tags.
    std::string tag;
    bool favouritesOnly = false;

    bool isActive() const { return ! tag.empty() || favouritesOnly; }
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
    // set() is the user's hand: it never marks a tag auto-assigned. A tag keeps the mark only when
    // it is auto-assigned on disk now and `info` keeps both the tag and the mark; dropping a tag's
    // mark (SoundInfo::confirmTag) makes it the user's own.

    struct AutoAssignment {
        SoundFingerprint fingerprint;
        std::vector<std::string> tags;
    };
    // Auto-tag: adds each sound's tags, marked auto-assigned, leaving out those the sound has on
    // disk by now (set in another window). One read and one write for all of them; on
    // RefusedUnreadable or Failed nothing changes.
    MetadataSaveResult autoAssign(const std::vector<AutoAssignment>& assignments);
    // Undo auto-tag: removes every auto-assigned tag; a sound left with no information is removed
    // from the file.
    MetadataSaveResult removeAutoAssigned();
    // The sounds with at least one auto-assigned tag.
    int autoAssignedCount() const;

    // The sound `soundId` was edited in the editor into `edited` (and saved): its fingerprint
    // becomes `edited`. Nothing happens when the sound is gone or another sound already has
    // exactly these values.
    MetadataSaveResult followEdit(const std::string& soundId, const SoundFingerprint& edited);

    // kLibraryTags followed by any other tag found in the file, sorted.
    std::vector<std::string> tagChoices() const;

    struct Sound {
        SoundFingerprint fingerprint;
        SoundInfo info;
    };
    // Every tagged sound, by id.
    const std::map<std::string, Sound>& taggedSounds() const { return sounds; }

private:
    // `versionOnDisk`: the format version of the file being replaced (0: none).
    MetadataSaveResult rewrite(const std::map<std::string, Sound>& newSounds, int versionOnDisk);
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
