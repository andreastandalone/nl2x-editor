#pragma once

#include "MidiPorts.h"

#include <juce_gui_basics/juce_gui_basics.h>

// The two port menus (from the synth, to the synth), shared by the editor and the Probe window.
// Every selector shows the same choice: a change in one updates the others.
class MidiPortSelector final : public juce::Component, private juce::ChangeListener {
public:
    explicit MidiPortSelector(MidiPorts& midiPorts);
    ~MidiPortSelector() override;

    void resized() override;

    static constexpr int kPreferredHeight = 2 * (24 + 26) + 12;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override { refresh(); }
    void refresh();

    MidiPorts& ports;
    juce::Label inputLabel { {}, "MIDI Input (from the synth)" };
    juce::Label outputLabel { {}, "MIDI Output (to the synth)" };
    juce::ComboBox inputPorts;
    juce::ComboBox outputPorts;
    juce::StringArray inputIdentifiers;
    juce::StringArray outputIdentifiers;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiPortSelector)
};
