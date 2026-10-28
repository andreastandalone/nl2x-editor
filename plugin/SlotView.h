#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cstdint>

// A plain, generic view of what the synth plays (#4): Slot buttons A–D, the 66 values of the
// selected Slot's Program with their names, "Read from synth", and the Global Channel and
// connection status. The panel layout replaces it in #13. It polls the engine's state revision
// on a timer and redraws only when it moved.
class SlotView final : public juce::Component, private juce::ListBoxModel, private juce::Timer {
public:
    explicit SlotView(NL2XEditorProcessor& owner);
    ~SlotView() override;

    void resized() override;

    // The Slot chosen with the Slot buttons: the Library auditions on it (#11).
    nl2x::Slot selected() const { return selectedSlot; }

private:
    // ListBoxModel
    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override;

    void timerCallback() override;
    void selectSlot(nl2x::Slot slot);
    void refresh();

    NL2XEditorProcessor& processor;
    nl2x::Slot selectedSlot = nl2x::Slot::A;
    std::uint64_t shownRevision = 0;

    std::array<juce::TextButton, 4> slotButtons;
    juce::TextButton readButton { "Read from synth" };
    juce::Label statusLabel;
    juce::Label noticeLabel;
    juce::ListBox valueList { "Program values", this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SlotView)
};
