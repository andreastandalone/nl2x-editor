#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

// A plain, generic view of what the synth plays: Slot buttons A–D, "Read from synth", the Global
// Channel and connection status, then the selected Slot's Program: every CC parameter as a plain
// control (slider, menu of named steps, or toggle) with its stored value as a number (#6), and
// the Morph Amounts, shown only until #7. Edits go through the DAW parameters, so a DAW
// recording automation records them. The panel layout replaces it in #13. It polls the engine's
// state revision on a timer and redraws only when it moved.
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
    struct MorphAmountRow {
        nl2x::ProgramParameter parameter;
        std::unique_ptr<juce::Label> name;
        std::unique_ptr<juce::Label> value;
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
    juce::Label statusLabel;
    juce::Label noticeLabel;

    juce::Viewport viewport;
    juce::Component content;
    juce::Label morphHeading { {}, "Morph Amounts (shown only: editing comes later)" };
    std::vector<std::unique_ptr<ParameterControl>> controls;
    std::vector<MorphAmountRow> morphRows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SlotView)
};
