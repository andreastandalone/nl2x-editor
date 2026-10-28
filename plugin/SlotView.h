#pragma once

#include "PluginProcessor.h"
#include "ProgramPanel.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <functional>

// The view of one Slot (the Slot tabs A–D): a tool row with "Read from synth", the Morph button
// and the connection status, then the Slot's Program on the front-panel layout (ProgramPanel), or
// a notice when the Slot plays a Percussion Kit or has not been read. Edits go through the DAW
// parameters, so a DAW recording automation records them. The Morph button switches every knob to
// editing its Morph Amount; Option-drag does it for one drag. It polls the engine's state revision
// on a timer and redraws only when it moved.
class SlotView final : public juce::Component, private juce::Timer {
public:
    explicit SlotView(NL2XEditorProcessor& owner);
    ~SlotView() override;

    static constexpr int kToolRowHeight = 28;
    static constexpr int kPreferredHeight = kToolRowHeight + 6 + ProgramPanel::kHeight;

    void resized() override;

    // The Slot shown: the Library auditions on it (#11), "Store" stores it (#12).
    nl2x::Slot selected() const { return selectedSlot; }
    void selectSlot(nl2x::Slot slot);
    // The user chose a Slot with the panel's Slot buttons; the owner selects the matching tab.
    std::function<void(nl2x::Slot)> onSlotChosen;

private:
    void timerCallback() override;
    void refresh();

    NL2XEditorProcessor& processor;
    nl2x::Slot selectedSlot = nl2x::Slot::A;
    std::uint64_t shownRevision = 0;

    juce::TextButton readButton { "Read from synth" };
    juce::TextButton morphButton { "Morph" };
    juce::Label statusLabel;
    juce::Label hintLabel;
    ProgramPanel panel;
    juce::TooltipWindow tooltips { this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SlotView)
};
