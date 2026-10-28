#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

// A stepped panel control shown as the synth shows it (user story 16): its named settings in a
// column (or several), each with an LED; the lit one is the current value. Clicking a name chooses
// it. A stored value outside the list lights no LED and is printed, marked, in place of the name.
// Flat, in NordLookAndFeel.h's colours. It only shows what it is given and reports edits.
class OptionList final : public juce::Component, public juce::SettableTooltipClient {
public:
    // `choices` name the values `minValue`, `minValue` + 1, …; `name` may be empty (the section's
    // title names it).
    OptionList(const juce::String& name, std::vector<juce::String> choices, int minValue, int columns = 1);

    void setValue(int newValue);
    int value() const { return currentValue; }

    // The height that shows the name and every choice.
    int preferredHeight() const;
    int preferredWidth() const;

    // The user chose a value (always one of the choices).
    std::function<void(int)> onValueEdit;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;

private:
    int rows() const;
    juce::Rectangle<int> choiceBounds(int index) const;

    const juce::String name;
    const std::vector<juce::String> choices;
    const int minValue;
    const int columns;
    int currentValue;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OptionList)
};
