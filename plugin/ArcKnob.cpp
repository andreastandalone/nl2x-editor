#include "ArcKnob.h"

#include "NordLookAndFeel.h"

#include "nl2x/HardwareAssumptions.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr int kMorphMin = -128;
constexpr int kMorphMax = 127;
// The arc spans 270 degrees, open at the bottom.
constexpr float kStartAngle = -0.75f * juce::MathConstants<float>::pi;
constexpr float kEndAngle = 0.75f * juce::MathConstants<float>::pi;
// Pixels of vertical drag for the whole range; Shift is this many times finer.
constexpr double kDragPixelsForRange = 200.0;
constexpr double kFineFactor = 5.0;
constexpr float kNameHeight = 24.0f;
constexpr float kValueHeight = 16.0f;

juce::String signedText(int amount)
{
    return amount > 0 ? "+" + juce::String(amount) : juce::String(amount);
}

juce::Colour morphColour(int amount)
{
    if (amount == 0)
        return NordPalette::textDim;
    return amount < 0 ? NordPalette::morphNegative : NordPalette::morph;
}

} // namespace

ArcKnob::ArcKnob(const juce::String& knobName, int minimum, int maximum, bool hasMorphAmount)
    : name(knobName),
      minValue(minimum),
      maxValue(maximum),
      morphable(hasMorphAmount),
      currentValue(minimum)
{
    setWantsKeyboardFocus(false);
    setRepaintsOnMouseActivity(false);
}

void ArcKnob::setValue(int newValue)
{
    if ((dragging && dragTarget == Target::Value) || newValue == currentValue)
        return;
    currentValue = newValue;
    repaint();
}

void ArcKnob::setMorphAmount(int newAmount)
{
    if ((dragging && dragTarget == Target::MorphAmount) || newAmount == currentMorph)
        return;
    currentMorph = newAmount;
    repaint();
}

void ArcKnob::setMorphMode(bool on)
{
    if (morphMode == on)
        return;
    morphMode = on;
    repaint();
}

ArcKnob::Target ArcKnob::targetFor(const juce::ModifierKeys& modifiers) const
{
    // Option is Alt to JUCE.
    return morphable && (morphMode || modifiers.isAltDown()) ? Target::MorphAmount : Target::Value;
}

bool ArcKnob::showsMorph() const
{
    if (! morphable)
        return false;
    if (dragging)
        return dragTarget == Target::MorphAmount;
    return morphMode || optionHeld;
}

double ArcKnob::positionOf(int shownValue) const
{
    if (maxValue <= minValue)
        return 0.0;
    const auto clipped = std::clamp(shownValue, minValue, maxValue);
    return static_cast<double>(clipped - minValue) / static_cast<double>(maxValue - minValue);
}

float ArcKnob::angleOf(double position) const
{
    return kStartAngle + static_cast<float>(position) * (kEndAngle - kStartAngle);
}

void ArcKnob::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    const auto text = NordPalette::text;
    const bool enabled = isEnabled();

    g.setColour(enabled ? NordPalette::textDim : NordPalette::textDim.withMultipliedAlpha(0.6f));
    g.setFont(NordFonts::controlName());
    g.drawFittedText(name, area.removeFromTop(kNameHeight).toNearestInt(), juce::Justification::centredBottom, 2,
                     0.9f);

    auto valueArea = area.removeFromBottom(kValueHeight);
    const auto size = std::min(area.getWidth(), area.getHeight()) - 4.0f;
    const auto knob = area.withSizeKeepingCentre(size, size);
    const auto centre = knob.getCentre();
    const auto radius = size * 0.5f - 4.0f;
    const auto thickness = std::max(3.0f, size * 0.09f);

    const auto arc = [&](float fromAngle, float toAngle, float arcRadius, float width, juce::Colour colour) {
        juce::Path path;
        path.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, fromAngle, toAngle, true);
        g.setColour(colour);
        g.strokePath(path, juce::PathStrokeType(width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };

    // Track, then the value from the start of the range.
    arc(kStartAngle, kEndAngle, radius, thickness, NordPalette::knobTrack);
    if (! enabled) {
        g.setFont(NordFonts::value());
        g.setColour(NordPalette::textDim);
        g.drawText(juce::String::fromUTF8("\xE2\x80\x93"), valueArea, juce::Justification::centred);
        return;
    }
    const auto valueAngle = angleOf(positionOf(currentValue));
    const bool outOfRange = currentValue < minValue || currentValue > maxValue;
    arc(kStartAngle, valueAngle, radius, thickness, outOfRange ? NordPalette::outOfRange : NordPalette::valueArc);

    // The cap in the middle, inside the Morph ring, with the pointer towards the value.
    const auto capRadius = radius - 2.0f * thickness - 1.0f;
    g.setColour(NordPalette::knobCap);
    g.fillEllipse(juce::Rectangle<float>(capRadius * 2.0f, capRadius * 2.0f).withCentre(centre));
    g.setColour(text);
    const auto pointer = juce::Point<float>(std::sin(valueAngle), -std::cos(valueAngle));
    g.drawLine(juce::Line<float>(centre + pointer * (capRadius * 0.3f), centre + pointer * (capRadius * 0.9f)), 2.0f);

    // The Morph Amount: inside the main arc, from the value towards where value + amount lands,
    // in the colour of its sign, with a dot at that end. When value + amount goes past the end of
    // the range, the ring stops at the end and a tick across the track marks the clipping, so a
    // Morph Amount is never invisible (a knob at its minimum with a negative amount shows the dot
    // and the tick).
    if (morphable && currentMorph != 0) {
        const auto from = std::clamp(currentValue, minValue, maxValue);
        const auto target = nl2x::hardware::morphTarget(from, currentMorph);
        const bool clipped = target < minValue || target > maxValue;
        const auto colour = morphColour(currentMorph);
        const auto targetAngle = angleOf(positionOf(target));
        const auto morphRadius = radius - thickness - 1.5f;
        const auto morphWidth = std::max(2.0f, thickness * 0.5f);
        if (std::clamp(target, minValue, maxValue) != from)
            arc(std::min(valueAngle, targetAngle), std::max(valueAngle, targetAngle), morphRadius, morphWidth, colour);

        const auto direction = juce::Point<float>(std::sin(targetAngle), -std::cos(targetAngle));
        g.setColour(colour);
        if (clipped)
            g.drawLine(juce::Line<float>(centre + direction * (morphRadius - morphWidth),
                                         centre + direction * (radius + thickness * 0.5f + 2.5f)),
                       2.0f);
        const auto dot = morphWidth + 1.0f;
        g.fillEllipse(juce::Rectangle<float>(dot * 2.0f, dot * 2.0f).withCentre(centre + direction * morphRadius));
    }

    // Underneath: the number being edited.
    g.setFont(NordFonts::value());
    if (showsMorph()) {
        g.setColour(morphColour(currentMorph));
        g.drawText("M " + signedText(currentMorph), valueArea, juce::Justification::centred);
    } else {
        g.setColour(outOfRange ? NordPalette::outOfRange : text);
        g.drawText(juce::String(currentValue) + (outOfRange ? " !" : ""), valueArea, juce::Justification::centred);
    }
}

void ArcKnob::edit(Target target, int newValue)
{
    if (target == Target::Value) {
        newValue = std::clamp(newValue, minValue, maxValue);
        if (newValue == currentValue)
            return;
        currentValue = newValue;
        if (onValueEdit)
            onValueEdit(newValue);
    } else {
        newValue = std::clamp(newValue, kMorphMin, kMorphMax);
        if (newValue == currentMorph)
            return;
        currentMorph = newValue;
        if (onMorphAmountEdit)
            onMorphAmountEdit(newValue);
    }
    repaint();
}

void ArcKnob::mouseDown(const juce::MouseEvent& event)
{
    if (! isEnabled())
        return;
    dragging = true;
    dragTarget = targetFor(event.mods);
    // A value outside the range starts from the range's nearest end.
    dragValue = dragTarget == Target::Value ? std::clamp(currentValue, minValue, maxValue) : currentMorph;
    lastDragY = event.getPosition().y;
    if (onGestureStart)
        onGestureStart();
    repaint();
}

void ArcKnob::mouseDrag(const juce::MouseEvent& event)
{
    if (! dragging)
        return;
    const auto y = event.getPosition().y;
    const auto range = dragTarget == Target::Value ? maxValue - minValue : kMorphMax - kMorphMin;
    auto perPixel = static_cast<double>(range) / kDragPixelsForRange;
    if (event.mods.isShiftDown())
        perPixel /= kFineFactor;
    dragValue += static_cast<double>(lastDragY - y) * perPixel;
    lastDragY = y;
    const auto lowest = dragTarget == Target::Value ? minValue : kMorphMin;
    const auto highest = dragTarget == Target::Value ? maxValue : kMorphMax;
    dragValue = std::clamp(dragValue, static_cast<double>(lowest), static_cast<double>(highest));
    edit(dragTarget, juce::roundToInt(dragValue));
}

void ArcKnob::mouseUp(const juce::MouseEvent&)
{
    if (! dragging)
        return;
    dragging = false;
    if (onGestureEnd)
        onGestureEnd();
    repaint();
}

void ArcKnob::mouseDoubleClick(const juce::MouseEvent& event)
{
    if (! isEnabled())
        return;
    if (targetFor(event.mods) == Target::MorphAmount)
        edit(Target::MorphAmount, 0);
}

void ArcKnob::mouseEnter(const juce::MouseEvent& event)
{
    modifierKeysChanged(event.mods);
}

void ArcKnob::mouseExit(const juce::MouseEvent&)
{
    modifierKeysChanged({});
}

void ArcKnob::modifierKeysChanged(const juce::ModifierKeys& modifiers)
{
    // Holding Option shows the Morph Amount the next drag will edit.
    if (const bool held = modifiers.isAltDown(); held != optionHeld) {
        optionHeld = held;
        repaint();
    }
}
