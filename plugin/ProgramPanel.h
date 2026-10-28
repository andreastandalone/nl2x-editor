#pragma once

#include "ArcKnob.h"
#include "LedSwitch.h"
#include "OptionList.h"

#include "nl2x/CcParameters.h"
#include "nl2x/Program.h"
#include "nl2x/Slot.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>
#include <vector>

// One Slot's Program laid out like the Nord Lead 2X front panel (spec #1, user stories 13–16 and
// 22; docs/design/visual-style.md): LFO 1, LFO 2/Arp and Mod Env on the left, the Oscillators in
// the centre, Amplifier and Filter on the right, and along the bottom Master Level, the Slot
// buttons A–D, Play Mode, Portamento, Unison, Oct Shift and Mod Wheel destination. Every CC
// parameter has exactly one control: knobs as arc knobs (each with its Morph Amount as a second
// arc), stepped buttons as lists of named settings with LEDs, on/off buttons as LED switches.
// Master Level has no MIDI [OFF-2X p.103] and is shown disabled.
//
// It shows the values it is given and reports edits through the callbacks; the owner routes them.
// With a notice (a Kit Slot, a Slot not read yet) the Program sections give way to the notice and
// only the Slot buttons stay.
class ProgramPanel final : public juce::Component {
public:
    ProgramPanel();
    ~ProgramPanel() override;

    // The fixed size the layout is drawn for.
    static constexpr int kWidth = 1072;
    static constexpr int kHeight = 556;

    // The Slot whose buttons A–D light up.
    void showSlot(nl2x::Slot slot);
    void showProgram(const nl2x::Program& program);
    // A non-empty notice replaces the Program sections.
    void showNotice(const juce::String& notice);
    void setMorphMode(bool on);

    // The user chose a Slot with the Slot buttons.
    std::function<void(nl2x::Slot)> onSlotChosen;
    std::function<void(nl2x::CcParameter, int)> onValueEdit;
    std::function<void(nl2x::ProgramParameter, int)> onMorphAmountEdit;
    // A knob drag: the edits between are one undo step.
    std::function<void()> onGestureStart;
    std::function<void()> onGestureEnd;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    struct Section {
        juce::String title;
        juce::Rectangle<int> bounds;
        // Hidden with the notice; the Slot section stays.
        bool program = true;
    };

    // Places a control at (x, y) inside `section`, at its preferred size or `width` when given.
    void place(nl2x::CcParameter parameter, const Section& section, int x, int y, int width = 0);
    void placeKnob(juce::Component& knob, const Section& section, int x, int y);
    void addSubtitle(const Section& section, const juce::String& text, int x, int y);

    std::array<std::unique_ptr<ArcKnob>, nl2x::kCcParameterCount> knobs;
    std::array<std::unique_ptr<OptionList>, nl2x::kCcParameterCount> lists;
    std::array<std::unique_ptr<LedSwitch>, nl2x::kCcParameterCount> switches;
    ArcKnob masterLevel;
    std::array<juce::TextButton, 4> slotButtons;
    std::vector<Section> sections;
    // Labels inside a section ("osc 1", "osc 2"), drawn in paint().
    struct Subtitle {
        juce::String text;
        juce::Point<int> position;
    };
    std::vector<Subtitle> subtitles;
    juce::String notice;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProgramPanel)
};
