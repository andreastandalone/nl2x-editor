#pragma once

// Suggested categories for Library sounds: a guess the Library shows apart from the user's own
// category, which the user accepts with a click or dismisses. A suggestion is stored only when the
// user keeps it (LibraryMetadata::set) or runs Auto-tag (LibraryMetadata::autoAssign).
//
// Four sources, in this order of precedence (the first that gives a category wins):
// 1. a Percussion Kit is always Percussion;
// 2. a file holding one sound is named by the user or the sound's author: a keyword in the name
//    ("Fat Bass", "Warm Pad", "Church Organ") names the category. In English compounds the last
//    keyword is the sound ("String Pad" is a Pad, "Arpeggio Bass" a Bass); a name joining two
//    categories with "/" says nothing;
// 3. the user's own categories (LearnedCategories), when enough Programs he tagged by hand are
//    close to this one and agree. Any category can come from here, Pad included, and it follows
//    the user's own way of sorting sounds, which is why it comes before the fixed rules;
// 4. the fixed rules on the parameters, and only for Bass and Keys, the two categories they
//    recognise reliably. Measured offline (2026-11-03) on 1794 Programs of the developer's Library
//    whose human-given names say what they are, on the half the rules were not written from: Bass
//    87 % right (finding 55 % of the basses), Keys 85 % (34 %). Pad, Lead, Brass/Strings, FX and
//    arpeggios stay below 60 % with any rule tried, so they are never suggested from rules.

#include "nl2x/Library.h"
#include "nl2x/LibraryMetadata.h"
#include "nl2x/Program.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nl2x {

// In order of precedence (see above).
enum class CategorySuggestionSource : std::uint8_t { PercussionKit, Name, Learned, Parameters };

struct CategorySuggestion {
    // One of kLibraryCategories.
    std::string category;
    CategorySuggestionSource source = CategorySuggestionSource::Name;

    friend bool operator==(const CategorySuggestion&, const CategorySuggestion&) = default;
};

// What the user's own categories teach: a nearest-neighbour vote among the Programs he tagged by
// hand, computed at run time from library.json; nothing is learned ahead of time or shipped.
// Auto-assigned categories teach nothing, or the suggestions would feed on themselves.
//
// Programs are compared on what the fixed rules read (envelopes, filter, Octave Shift, Voice Mode,
// noise, modulation, arpeggiator), each scaled to 0 … 1 and weighted. A category is suggested only
// when at least kMinLearnedAgreement of the kLearnedNeighbours closest tagged Programs carry it and
// the closest one is near (kMaxLearnedDistance, in the scaled and weighted values). So nothing is
// learned before the user has tagged kLearnedNeighbours Programs, and a category only once he has
// tagged kMinLearnedAgreement Programs with it.
//
// Measured offline (2026-11-04) on the same 1794 Programs, simulating a user who tagged part of
// them and asking for the rest (near-copies of a sound always on the same side, 10 random splits):
// with half tagged, 86 % of learned suggestions are right and 22 % of the untagged sounds get one
// (Bass 92 %, Keys 85 %, Brass/Strings 90 %, Pad 73 %, Lead 68 %, the last three rarely); with a
// quarter tagged 87 % (18 %), with a tenth 82 % (11 %). Together with the fixed rules after them,
// 83 % right for 32 % of the sounds, against 86 % for 26 % from the rules alone.
class LearnedCategories {
public:
    static constexpr int kLearnedNeighbours = 9;
    static constexpr int kMinLearnedAgreement = 8;
    static constexpr float kMaxLearnedDistance = 1.1f;

    struct Example {
        Program program;
        std::string category;
    };

    // Learns nothing: never suggests.
    LearnedCategories() = default;
    explicit LearnedCategories(const std::vector<Example>& examples);
    // From the Programs in `metadata` whose category the user set by hand.
    static LearnedCategories of(const LibraryMetadata& metadata);

    std::optional<std::string> categoryOf(const Program& program) const;

private:
    // Per example: its scaled values, one after the other, and its category as an index into
    // `categories`.
    std::vector<float> features;
    std::vector<std::size_t> labels;
    std::vector<std::string> categories;
};

// The category a sound's name says, if any keyword in it names one.
std::optional<std::string> categoryFromName(std::string_view name);

// Bass or Keys when the parameters say so clearly; empty otherwise.
std::optional<std::string> categoryFromParameters(const Program& program);

// The suggestion for a Library entry of `file`. The name is used only when the file holds this
// one sound: a Bank file's name describes a collection, and its entries are only numbered.
// Performances get none.
std::optional<CategorySuggestion> suggestCategory(const LibraryFile& file, const LibraryEntry& entry,
                                                  const LearnedCategories& learned = LearnedCategories {});

// The suggestion for a Program known only by its parameters (a Bank location on the synth).
std::optional<CategorySuggestion> suggestCategory(const Program& program,
                                                  const LearnedCategories& learned = LearnedCategories {});

} // namespace nl2x
