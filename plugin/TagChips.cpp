#include "TagChips.h"

#include "NordLookAndFeel.h"

#include <algorithm>

namespace {

juce::String textOf(const TagChip& chip)
{
    const auto text = juce::String::fromUTF8(chip.tag.c_str());
    return chip.style == TagChip::Style::Suggested ? NordLookAndFeel::suggestedTagText(text) : text;
}

juce::String moreText(int hidden)
{
    return "+" + juce::String(hidden);
}

} // namespace

void TagChipRow::layout(const std::vector<TagChip>& chips, juce::Rectangle<int> area, const Layout& options)
{
    placed.clear();
    hidden = 0;
    more = {};
    addLabel = options.addLabel;
    const int gap = NordMetrics::tagGap;
    const int addWidth = std::min(area.getWidth(), chips.empty() ? options.emptyAddWidth : options.addWidth);
    const auto tagHeight = static_cast<float>(std::min(area.getHeight() - 6, NordMetrics::tagHeight));
    const auto tagTop = static_cast<float>(area.getY()) + (static_cast<float>(area.getHeight()) - tagHeight) * 0.5f;
    auto row = area;
    const auto addZone = row.removeFromRight(addWidth);
    add = juce::Rectangle<float>(static_cast<float>(addZone.getX()), tagTop, static_cast<float>(addZone.getWidth()), tagHeight);

    // How many chips fit in order, with room for "+N" when some do not.
    const int room = row.getWidth() - options.minTextWidth;
    std::vector<int> widths;
    for (const auto& chip : chips)
        widths.push_back(NordLookAndFeel::tagWidth(textOf(chip)));
    std::size_t shown = 0;
    int used = 0;
    for (; shown < chips.size(); ++shown) {
        const int next = used + widths[shown] + gap;
        const auto rest = chips.size() - shown - 1;
        const int moreRoom = rest > 0 ? NordLookAndFeel::tagWidth(moreText(static_cast<int>(rest))) + gap : 0;
        if (next + moreRoom > room)
            break;
        used = next;
    }
    hidden = static_cast<int>(chips.size() - shown);
    const int moreWidth = hidden > 0 ? NordLookAndFeel::tagWidth(moreText(hidden)) + gap : 0;

    // In order, right-aligned before the add zone, or from the left with the add zone after them.
    int x = options.leftAligned ? row.getX() : row.getRight() - used - moreWidth;
    for (std::size_t i = 0; i < shown; ++i) {
        placed.push_back({ chips[i], { static_cast<float>(x), tagTop, static_cast<float>(widths[i]), tagHeight } });
        x += widths[i] + gap;
    }
    if (hidden > 0)
        more = { static_cast<float>(x), tagTop, static_cast<float>(moreWidth - gap), tagHeight };
    if (options.leftAligned)
        add.setX(static_cast<float>(x + moreWidth));
    text = row.withRight(std::max(row.getX(), row.getRight() - used - moreWidth - gap));
}

void TagChipRow::paint(juce::Graphics& g, bool showAdd) const
{
    for (const auto& [chip, area] : placed) {
        const auto label = juce::String::fromUTF8(chip.tag.c_str());
        switch (chip.style) {
        case TagChip::Style::Own:
            NordLookAndFeel::drawTag(g, label, area, true);
            break;
        case TagChip::Style::Auto:
            NordLookAndFeel::drawAutoTag(g, label, area);
            break;
        case TagChip::Style::Suggested:
            NordLookAndFeel::drawSuggestedTag(g, label, area);
            break;
        case TagChip::Style::Remark:
            NordLookAndFeel::drawTag(g, label, area, false);
            break;
        }
    }
    if (hidden > 0)
        NordLookAndFeel::drawTag(g, moreText(hidden), more, false);
    if (showAdd || addLabel.isNotEmpty())
        NordLookAndFeel::drawAddTag(g, addLabel.isEmpty() ? add : add.withSizeKeepingCentre(add.getWidth() - 2.0f, add.getHeight()),
                                    addLabel);
}

TagChipRow::Hit TagChipRow::hitTest(juce::Point<float> point) const
{
    for (const auto& [chip, area] : placed) {
        if (! area.expanded(NordMetrics::tagGap * 0.5f, 2.0f).contains(point))
            continue;
        switch (chip.style) {
        case TagChip::Style::Own:
        case TagChip::Style::Auto:
            return { Hit::Kind::Tag, chip.tag };
        case TagChip::Style::Suggested:
            return { Hit::Kind::Suggested, chip.tag };
        case TagChip::Style::Remark:
            return {};
        }
    }
    if (hidden > 0 && more.expanded(NordMetrics::tagGap * 0.5f, 2.0f).contains(point))
        return { Hit::Kind::More, {} };
    // The whole height of the row at the add zone.
    if (point.x >= add.getX() && point.x < add.getRight())
        return { Hit::Kind::Add, {} };
    return {};
}
