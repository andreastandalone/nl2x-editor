#include "OptionList.h"

#include "NordLookAndFeel.h"

namespace {

constexpr float kLedSize = 7.0f;
constexpr int kLedSpace = 13;

} // namespace

OptionList::OptionList(const juce::String& listName, std::vector<juce::String> names, int minimum, int columnCount)
    : name(listName),
      choices(std::move(names)),
      minValue(minimum),
      columns(std::max(1, columnCount)),
      currentValue(minimum)
{
    setWantsKeyboardFocus(false);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void OptionList::setValue(int newValue)
{
    if (newValue == currentValue)
        return;
    currentValue = newValue;
    repaint();
}

int OptionList::rows() const
{
    return (static_cast<int>(choices.size()) + columns - 1) / columns;
}

int OptionList::preferredHeight() const
{
    // The name row is there even without a name: it shows a value outside the list.
    return NordMetrics::choiceRowHeight + 2 + rows() * NordMetrics::choiceRowHeight;
}

int OptionList::preferredWidth() const
{
    const juce::Font font(NordFonts::choice());
    int widest = 0;
    for (const auto& choice : choices)
        widest = std::max(widest, juce::GlyphArrangement::getStringWidthInt(font, choice));
    const int column = kLedSpace + widest + 8;
    const int nameWidth = juce::GlyphArrangement::getStringWidthInt(juce::Font(NordFonts::controlName()), name) + 4;
    return std::max(column * columns, nameWidth);
}

juce::Rectangle<int> OptionList::choiceBounds(int index) const
{
    auto area = getLocalBounds();
    area.removeFromTop(NordMetrics::choiceRowHeight + 2);
    const int columnWidth = area.getWidth() / columns;
    const int column = index / rows();
    const int row = index % rows();
    return { area.getX() + column * columnWidth, area.getY() + row * NordMetrics::choiceRowHeight, columnWidth,
             NordMetrics::choiceRowHeight };
}

void OptionList::paint(juce::Graphics& g)
{
    const auto index = currentValue - minValue;
    const bool inRange = index >= 0 && index < static_cast<int>(choices.size());

    if (name.isNotEmpty() || ! inRange) {
        auto nameRow = getLocalBounds().removeFromTop(NordMetrics::choiceRowHeight);
        g.setFont(NordFonts::controlName());
        if (inRange) {
            g.setColour(NordPalette::textDim);
            g.drawText(name, nameRow, juce::Justification::centredLeft, true);
        } else {
            g.setColour(NordPalette::outOfRange);
            g.drawText((name.isNotEmpty() ? name + ": " : juce::String()) + juce::String(currentValue) + " !", nameRow,
                       juce::Justification::centredLeft, true);
        }
    }

    g.setFont(NordFonts::choice());
    for (int i = 0; i < static_cast<int>(choices.size()); ++i) {
        auto row = choiceBounds(i);
        const bool on = i == index;
        NordLookAndFeel::drawLed(g, row.removeFromLeft(kLedSpace).toFloat().withSizeKeepingCentre(kLedSize, kLedSize),
                                 on);
        g.setColour(on ? NordPalette::text : NordPalette::textDim);
        g.drawText(choices[static_cast<std::size_t>(i)], row, juce::Justification::centredLeft, true);
    }
}

void OptionList::mouseDown(const juce::MouseEvent& event)
{
    for (int i = 0; i < static_cast<int>(choices.size()); ++i)
        if (choiceBounds(i).contains(event.getPosition())) {
            const auto chosen = minValue + i;
            if (chosen == currentValue)
                return;
            currentValue = chosen;
            repaint();
            if (onValueEdit)
                onValueEdit(chosen);
            return;
        }
}
