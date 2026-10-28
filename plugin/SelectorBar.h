#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cstdint>

// The Bank/Program selector of each Slot and the Performance selector (#10), shown above the
// Slot and Performance tabs. Choosing a Bank or a Program (or Performance) sends the selection at
// once; the menus show what the engine reports as selected, blank when nothing is known. Plain
// look; styling is #13. It polls the engine's state revision.
class SelectorBar final : public juce::Component, private juce::Timer {
public:
    explicit SelectorBar(NL2XEditorProcessor& owner);
    ~SelectorBar() override;

    static constexpr int kPreferredHeight = 62;

    void resized() override;

private:
    struct SlotSelector {
        juce::Label name;
        juce::ComboBox bank;
        juce::ComboBox program;
    };

    void timerCallback() override;
    void refresh();
    void slotChosen(nl2x::Slot slot);
    void performanceChosen();
    void showResult(nl2x::SelectionResult result, const juce::String& what);

    NL2XEditorProcessor& processor;
    std::uint64_t shownRevision = 0;

    juce::Label performanceLabel { {}, "Performance" };
    juce::ComboBox performanceBank;
    juce::ComboBox performanceNumber;
    juce::Label resultLabel;
    std::array<SlotSelector, 4> slotSelectors;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SelectorBar)
};
