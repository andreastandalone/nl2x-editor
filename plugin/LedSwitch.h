#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

// An on/off panel button shown flat: an LED and the button's name, as on the synth. Clicking it
// switches it. A stored value other than 0 shows as on. It only shows what it is given and reports
// edits.
class LedSwitch final : public juce::Component, public juce::SettableTooltipClient {
public:
    explicit LedSwitch(const juce::String& name);

    void setValue(int newValue);
    int value() const { return currentValue; }
    int preferredWidth() const;

    // The user switched it: 1 = on, 0 = off.
    std::function<void(int)> onValueEdit;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;

private:
    const juce::String name;
    int currentValue = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LedSwitch)
};
