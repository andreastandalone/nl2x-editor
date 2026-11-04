#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <string>
#include <vector>

// One chip of a sound's tag row: a tag of the user's own, one given by Auto-tag, a suggested tag,
// or a remark such as "changed" that is no tag.
struct TagChip {
    enum class Style : std::uint8_t { Own, Auto, Suggested, Remark };
    // The tag (UTF-8); for a remark, its text.
    std::string tag;
    Style style = Style::Own;
};

// The tags of a sound laid out at the right end of a row (a Library row, a Librarian location, the
// Library's edit area), where they are drawn and clicked:
// [ text ......... ][tag][tag][+2][ + ]
// As many chips as fit in order, then "+N" for the rest; the last part ("+", the add zone) is where
// a tag is added, and is wider while the sound has no tag.
class TagChipRow {
public:
    struct Layout {
        // Left for the text, at least this wide while it can be.
        int minTextWidth = 70;
        // The add zone at the right end; `emptyAddWidth` while there is no chip.
        int addWidth = 18;
        int emptyAddWidth = 40;
        // Drawn in the add zone: empty for a dim "+", else an outlined chip with this text.
        juce::String addLabel;
        // From the left, the add zone right after the chips (where no text shares the row).
        bool leftAligned = false;
    };

    void layout(const std::vector<TagChip>& chips, juce::Rectangle<int> area, const Layout& options);
    // `showAdd`: draw the add zone (a selected or hovered row).
    void paint(juce::Graphics& g, bool showAdd) const;
    // The part of the row left for the text.
    juce::Rectangle<int> textArea() const { return text; }

    struct Hit {
        enum class Kind : std::uint8_t { None, Tag, Suggested, More, Add };
        Kind kind = Kind::None;
        std::string tag;
    };
    // What a click at `point` (in the row's coordinates) is on. A remark is no hit.
    Hit hitTest(juce::Point<float> point) const;

private:
    struct Placed {
        TagChip chip;
        juce::Rectangle<float> area;
    };
    std::vector<Placed> placed;
    int hidden = 0;
    juce::Rectangle<float> more;
    juce::Rectangle<float> add;
    juce::Rectangle<int> text;
    juce::String addLabel;
};
