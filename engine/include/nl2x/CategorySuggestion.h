#pragma once

// Suggested categories for Library sounds: a guess the Library shows apart from the user's own
// category, which the user accepts with a click or dismisses. A suggestion is never stored on its
// own; only accepting it writes the category (LibraryMetadata::set).
//
// Three sources, in order of trust:
// - a Percussion Kit is always Percussion;
// - a file holding one sound is named by the user or the sound's author: a keyword in the name
//   ("Fat Bass", "Warm Pad", "Church Organ") names the category. In English compounds the last
//   keyword is the sound ("String Pad" is a Pad, "Arpeggio Bass" a Bass); a name joining two
//   categories with "/" says nothing;
// - otherwise the parameters, and only for Bass and Keys, the two categories hand-written rules
//   recognise reliably. Measured offline (2026-11-03) on 1794 Programs of the developer's Library
//   whose human-given names say what they are, on the half the rules were not written from: Bass
//   87 % right (finding 55 % of the basses), Keys 85 % (34 %). Pad, Lead, Brass/Strings, FX and
//   arpeggios stay below 60 % with any rule tried, so they are never suggested from parameters.

#include "nl2x/Library.h"
#include "nl2x/Program.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace nl2x {

enum class CategorySuggestionSource : std::uint8_t { PercussionKit, Name, Parameters };

struct CategorySuggestion {
    // One of kLibraryCategories.
    std::string category;
    CategorySuggestionSource source = CategorySuggestionSource::Name;

    friend bool operator==(const CategorySuggestion&, const CategorySuggestion&) = default;
};

// The category a sound's name says, if any keyword in it names one.
std::optional<std::string> categoryFromName(std::string_view name);

// Bass or Keys when the parameters say so clearly; empty otherwise.
std::optional<std::string> categoryFromParameters(const Program& program);

// The suggestion for a Library entry of `file`. The name is used only when the file holds this
// one sound: a Bank file's name describes a collection, and its entries are only numbered.
// Performances get none.
std::optional<CategorySuggestion> suggestCategory(const LibraryFile& file, const LibraryEntry& entry);

// The suggestion for a Program known only by its parameters (a Bank location on the synth).
std::optional<CategorySuggestion> suggestCategory(const Program& program);

} // namespace nl2x
