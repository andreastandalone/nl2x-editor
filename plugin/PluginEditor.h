#pragma once

#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

// The first, functional GUI: one knob and the MIDI port settings. The panel-style layout in Nord
// colours is #13.
class NL2XPluginEditor final : public juce::AudioProcessorEditor {
public:
    explicit NL2XPluginEditor(NL2XEditorProcessor& owner);
    ~NL2XPluginEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void refreshPortLists();

    NL2XEditorProcessor& editorProcessor;

    juce::Label slotTitle { {}, "Slot A" };
    juce::Slider cutoffKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Label cutoffLabel;
    juce::AudioProcessorValueTreeState::SliderAttachment cutoffAttachment;

    juce::Label settingsTitle { {}, "MIDI settings" };
    juce::Label inputLabel { {}, "MIDI Input (from the synth)" };
    juce::Label outputLabel { {}, "MIDI Output (to the synth)" };
    juce::ComboBox inputPorts;
    juce::ComboBox outputPorts;
    juce::StringArray inputIdentifiers;
    juce::StringArray outputIdentifiers;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NL2XPluginEditor)
};
