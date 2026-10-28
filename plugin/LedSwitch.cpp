#include "LedSwitch.h"

#include "NordLookAndFeel.h"

namespace {

constexpr float kLedSize = 8.0f;

} // namespace

LedSwitch::LedSwitch(const juce::String& switchName)
    : name(switchName)
{
    setWantsKeyboardFocus(false);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void LedSwitch::setValue(int newValue)
{
    if (newValue == currentValue)
        return;
    currentValue = newValue;
    repaint();
}

int LedSwitch::preferredWidth() const
{
    return juce::GlyphArrangement::getStringWidthInt(juce::Font(NordFonts::choice()), name) + 30;
}

void LedSwitch::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour(NordPalette::control);
    g.fillRoundedRectangle(area, area.getHeight() * 0.5f);
    area.removeFromLeft(4.0f);
    NordLookAndFeel::drawLed(g, area.removeFromLeft(kLedSize + 6.0f).withSizeKeepingCentre(kLedSize, kLedSize),
                             currentValue != 0);
    g.setColour(currentValue != 0 ? NordPalette::text : NordPalette::textDim);
    g.setFont(NordFonts::choice());
    g.drawFittedText(name, area.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
}

void LedSwitch::mouseDown(const juce::MouseEvent&)
{
    currentValue = currentValue != 0 ? 0 : 1;
    repaint();
    if (onValueEdit)
        onValueEdit(currentValue);
}
