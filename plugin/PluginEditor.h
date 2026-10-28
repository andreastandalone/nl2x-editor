#pragma once

#include "MidiPortSelector.h"
#include "PluginProcessor.h"
#include "ProbeWindow.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

// The first, functional GUI: one knob and the MIDI port settings. The panel-style layout in Nord
// colours is #13. In the standalone app only, a Probe button opens the hardware discovery probe
// (#3).
class NL2XPluginEditor final : public juce::AudioProcessorEditor {
public:
    explicit NL2XPluginEditor(NL2XEditorProcessor& owner);
    ~NL2XPluginEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void showProbe();

    NL2XEditorProcessor& editorProcessor;

    juce::Label slotTitle { {}, "Slot A" };
    juce::Slider cutoffKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Label cutoffLabel;
    juce::AudioProcessorValueTreeState::SliderAttachment cutoffAttachment;

    juce::Label settingsTitle { {}, "MIDI settings" };
    MidiPortSelector portSelector;
    juce::TextButton probeButton { "Probe" };
    std::unique_ptr<ProbeWindow> probeWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NL2XPluginEditor)
};
