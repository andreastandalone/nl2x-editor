#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

// A flat arc knob (docs/design/visual-style.md): the name above, an arc for the value and the
// value printed underneath; no shading, bevels or textures. On a morphable knob a second, thinner
// arc in blue-violet shows the Morph Amount, from the value towards value + amount, clipped to the
// range (spec #1, user story 22).
//
// Dragging up or down edits the value (the mouse wheel is left to scrolling); with Morph mode on
// (setMorphMode, the Morph button) or with Option held when the drag starts, it edits the Morph
// Amount instead (user stories 23–24). Shift drags finely; double-click resets the Morph Amount to 0 when it is the one being edited. The
// knob only shows what it is given and reports edits through the callbacks: the owner routes them
// (through the DAW parameters) and feeds the engine's values back. Reusable for the front-panel
// layout (#13): it knows nothing about Slots or the engine.
class ArcKnob final : public juce::Component, public juce::SettableTooltipClient {
public:
    // The palette of the plugin; exact values follow from a photo of the synth (visual-style.md).
    static const juce::Colour kNordRed;
    static const juce::Colour kPanelGrey;
    static const juce::Colour kMorphViolet;
    static const juce::Colour kOutOfRange;

    // `minValue` … `maxValue`: the documented range. `morphable`: it has a Morph Amount, −128…127.
    ArcKnob(const juce::String& name, int minValue, int maxValue, bool morphable);

    // The value as stored; one outside the range is drawn at the range's end and printed marked.
    // Ignored while the user drags the value, so the engine's echo never fights the mouse.
    void setValue(int newValue);
    void setMorphAmount(int newAmount);
    int value() const { return currentValue; }
    int morphAmount() const { return currentMorph; }
    bool isMorphable() const { return morphable; }

    // Drags edit the Morph Amount; the value underneath shows the Morph Amount.
    void setMorphMode(bool on);

    // The user changed the value or the Morph Amount (already within its range).
    std::function<void(int)> onValueEdit;
    std::function<void(int)> onMorphAmountEdit;
    // A press starts and its release ends one gesture: the edits between are one undo step (#14).
    std::function<void()> onGestureStart;
    std::function<void()> onGestureEnd;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseEnter(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void modifierKeysChanged(const juce::ModifierKeys& modifiers) override;

private:
    enum class Target { Value, MorphAmount };

    Target targetFor(const juce::ModifierKeys& modifiers) const;
    bool showsMorph() const;
    void edit(Target target, int newValue);
    float angleOf(double position) const;
    double positionOf(int shownValue) const;

    const juce::String name;
    const int minValue;
    const int maxValue;
    const bool morphable;
    int currentValue;
    int currentMorph = 0;
    bool morphMode = false;
    bool optionHeld = false;
    // While dragging: what the drag edits, where it started and the fractional value reached.
    bool dragging = false;
    Target dragTarget = Target::Value;
    double dragValue = 0.0;
    int lastDragY = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ArcKnob)
};
