#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>

// The Connection check panel (user stories 6–9): the engine's results, one per test, each with
// what was found and what to change on the synth, and the interactive step (play a key, turn a
// knob) with a Skip button. Shown over the Slot list; polls the engine on a timer.
class ConnectionCheckView final : public juce::Component, private juce::Timer {
public:
    explicit ConnectionCheckView(NL2XEditorProcessor& owner);
    ~ConnectionCheckView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void refresh();
    void showResults();

    NL2XEditorProcessor& processor;
    std::uint64_t shownRevision = 0;
    juce::String shownPrompt;

    juce::Label title { {}, "Connection check" };
    juce::Label intro;
    juce::TextEditor results;
    juce::Label prompt;
    juce::TextButton skipButton { "Skip" };
    juce::TextButton runButton { "Run the check again" };
    juce::TextButton closeButton { "Close" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ConnectionCheckView)
};
