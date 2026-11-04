#pragma once

// Suggested tags for Library sounds: guesses the Library shows beside the user's own tags, which
// the user accepts with a click or dismisses. A suggestion is stored only when the user keeps it
// (LibraryMetadata::set) or runs Auto-tag (LibraryMetadata::autoAssign). A sound may get several.
//
// Four sources, in this order of precedence:
// 1. a Percussion Kit is always Percussion, and gets nothing else;
// 2. a file holding one sound is named by the user or the sound's author: keywords in the name
//    ("Fat Bass", "Warm Pad", "Church Organ") name what the sound is. In English compounds the
//    last such keyword is the sound ("String Pad" is a Pad, "Arpeggio Bass" a Bass), and a name
//    joining two kinds of sound with "/" says neither. Words that say how a sound plays ("Arp",
//    "Seq", "Sweep", "Synth", "Poly", "Mono", "Ambient") add their tag wherever they are;
// 3. the user's own tags (LearnedTags), each learned on its own, when enough Programs he tagged by
//    hand are close to this one and agree. Any tag can come from here, Pad and his own included,
//    and these are added to those of the name;
// 4. the fixed rules on the parameters, only for Bass and Keys, the two they recognise reliably,
//    and only when no other source suggests anything. Measured offline (2026-11-03) on 1794
//    Programs of the developer's Library whose human-given names say what they are, on the half
//    the rules were not written from: Bass 87 % right (finding 55 % of the basses), Keys 85 % (34 %).
//    Pad, Lead, Brass/Strings, FX and arpeggios stay below 60 % with any rule tried, so they are
//    never suggested from rules.
// A suggestion the sound already has as a tag, or one the user dismissed, is the caller's to leave
// out (the Library panel does).

#include "nl2x/Library.h"
#include "nl2x/LibraryMetadata.h"
#include "nl2x/Program.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace nl2x {

// In order of precedence (see above).
enum class TagSuggestionSource : std::uint8_t { PercussionKit, Name, Learned, Parameters };

struct TagSuggestion {
    std::string tag;
    TagSuggestionSource source = TagSuggestionSource::Name;

    friend bool operator==(const TagSuggestion&, const TagSuggestion&) = default;
};

// What the user's own tags teach: a nearest-neighbour vote among the Programs he tagged by hand,
// computed at run time from library.json; nothing is learned ahead of time or shipped. Auto-assigned
// tags teach nothing, or the suggestions would feed on themselves.
//
// Programs are compared on what the fixed rules read (envelopes, filter, Octave Shift, Voice Mode,
// noise, modulation, arpeggiator), each scaled to 0 … 1 and weighted. Each tag is judged on its
// own (one against the rest): it is suggested when at least kMinLearnedAgreement of the
// kLearnedNeighbours closest tagged Programs carry it and the closest one is near
// (kMaxLearnedDistance, in the scaled and weighted values). So nothing is learned before the user
// has tagged kLearnedNeighbours Programs, and a tag only once he has given it to
// kMinLearnedAgreement Programs.
//
// Measured offline (2026-11-04) when every sound had one category, on the same 1794 Programs,
// simulating a user who tagged part of them and asking for the rest (near-copies of a sound always
// on the same side, 10 random splits): with half tagged, 86 % of learned suggestions are right and
// 22 % of the untagged sounds get one (Bass 92 %, Keys 85 %, Brass/Strings 90 %, Pad 73 %, Lead
// 68 %, the last three rarely); with a quarter tagged 87 % (18 %), with a tenth 82 % (11 %). With
// one tag per sound the vote per tag gives the same suggestions as that vote among categories.
class LearnedTags {
public:
    static constexpr int kLearnedNeighbours = 9;
    static constexpr int kMinLearnedAgreement = 8;
    static constexpr float kMaxLearnedDistance = 1.1f;

    struct Example {
        Program program;
        std::vector<std::string> tags;
    };

    // Learns nothing: never suggests.
    LearnedTags() = default;
    explicit LearnedTags(const std::vector<Example>& examples);
    // From the Programs in `metadata` with tags the user set by hand (only those tags).
    static LearnedTags of(const LibraryMetadata& metadata);

    // The tags enough neighbours agree on, the most agreed first.
    std::vector<std::string> tagsOf(const Program& program) const;

private:
    // Per example: its scaled values, one after the other, and its tags as indexes into `tags`.
    std::vector<float> features;
    std::vector<std::vector<std::size_t>> labels;
    std::vector<std::string> tags;
};

// The tags a sound's name says: what it is (at most one kind of sound, which may give two tags, as
// Organ gives Keys and Organ), then how it plays.
std::vector<std::string> tagsFromName(std::string_view name);

// Bass or Keys when the parameters say so clearly; empty otherwise.
std::vector<std::string> tagsFromParameters(const Program& program);

// The suggestions for a Library entry of `file`, by precedence, each tag once. The name is used
// only when the file holds this one sound: a Bank file's name describes a collection, and its
// entries are only numbered. Performances get none.
std::vector<TagSuggestion> suggestTags(const LibraryFile& file, const LibraryEntry& entry,
                                       const LearnedTags& learned = LearnedTags {});

// The suggestions for a Program known only by its parameters (a Bank location on the synth).
std::vector<TagSuggestion> suggestTags(const Program& program, const LearnedTags& learned = LearnedTags {});

} // namespace nl2x
