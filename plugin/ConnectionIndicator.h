#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

// Always visible in the editor (user story 10): a coloured dot and a short text saying whether
// the synth is connected and answering, with the Global Channel as the synth shows it. Polls the
// processor on a timer.
class ConnectionIndicator final : public juce::Component, private juce::Timer {
public:
    explicit ConnectionIndicator(NL2XEditorProcessor& owner);
    ~ConnectionIndicator() override;

    void paint(juce::Graphics& g) override;

private:
    void timerCallback() override;

    NL2XEditorProcessor& processor;
    juce::Colour colour;
    juce::String text;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ConnectionIndicator)
};
