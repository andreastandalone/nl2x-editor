#pragma once

#include "ArcKnob.h"
#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

// A plain, generic view of what the synth plays: Slot buttons A–D, "Read from synth", the Global
// Channel and connection status, then the selected Slot's Program: the knobs as arc knobs, each
// morphable one with its Morph Amount as a second arc (#7), and the stepped and on/off buttons
// as a menu or toggle with the stored value as a number (#6). The Morph button switches every
// knob to editing its Morph Amount; Option-drag does it for one drag. Edits go through the DAW
// parameters, so a DAW recording automation records them. The panel layout replaces it in #13.
// It polls the engine's state revision on a timer and redraws only when it moved.
class SlotView final : public juce::Component, private juce::Timer {
public:
    explicit SlotView(NL2XEditorProcessor& owner);
    ~SlotView() override;

    void resized() override;

    // The Slot chosen with the Slot buttons: the Library auditions on it (#11).
    nl2x::Slot selected() const { return selectedSlot; }

    // One control for one CC parameter of the selected Slot.
    class ParameterControl;

private:
    // An arc knob for a knob-kind CC parameter.
    struct Knob {
        const nl2x::CcParameterInfo* info;
        std::unique_ptr<ArcKnob> component;
    };

    void timerCallback() override;
    void selectSlot(nl2x::Slot slot);
    void refresh();
    void layOutContent();

    NL2XEditorProcessor& processor;
    nl2x::Slot selectedSlot = nl2x::Slot::A;
    std::uint64_t shownRevision = 0;

    std::array<juce::TextButton, 4> slotButtons;
    juce::TextButton readButton { "Read from synth" };
    juce::TextButton morphButton { "Morph" };
    juce::Label statusLabel;
    juce::Label noticeLabel;

    juce::Viewport viewport;
    juce::Component content;
    std::vector<Knob> knobs;
    juce::TooltipWindow tooltips { this };
    std::vector<std::unique_ptr<ParameterControl>> controls;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SlotView)
};
