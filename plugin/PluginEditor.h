#pragma once

#include "MidiPortSelector.h"
#include "PerformanceView.h"
#include "PluginProcessor.h"
#include "ProbeWindow.h"
#include "SlotView.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

// The first, functional GUI: one knob, the MIDI port settings, the Slot Channel warning, and tabs
// with a generic list of the four Slots' values read from the synth (#4) and the Performance (#5).
// The panel-style layout in Nord colours is #13. In the standalone app only, a Probe button opens
// the hardware discovery probe (#3).
class NL2XPluginEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit NL2XPluginEditor(NL2XEditorProcessor& owner);
    ~NL2XPluginEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void showProbe();
    // Shows which Slots a knob change also reaches (shared channels, Layer).
    void timerCallback() override;

    NL2XEditorProcessor& editorProcessor;

    juce::Label slotTitle { {}, "Slot A" };
    juce::Slider cutoffKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Label cutoffLabel;
    juce::AudioProcessorValueTreeState::SliderAttachment cutoffAttachment;

    juce::Label settingsTitle { {}, "MIDI settings" };
    MidiPortSelector portSelector;
    juce::TextButton probeButton { "Probe" };
    juce::Label warningLabel;
    std::uint64_t shownRevision = 0;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    SlotView slotView;
    PerformanceView performanceView;
    std::unique_ptr<ProbeWindow> probeWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NL2XPluginEditor)
};
