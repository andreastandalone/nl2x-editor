#pragma once

#include "ConnectionCheckView.h"
#include "ConnectionIndicator.h"
#include "LibraryPanel.h"
#include "MidiPortSelector.h"
#include "PerformanceView.h"
#include "PluginProcessor.h"
#include "ProbeWindow.h"
#include "SelectorBar.h"
#include "SlotView.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <cstdint>
#include <memory>

// The first, functional GUI: one knob, the MIDI settings (ports, Global Channel), the connection
// indicator and Connection check (#9), the Slot Channel warning, and tabs with a generic list of
// the four Slots' values read from the synth (#4) and the Performance (#5), and the Library panel at the side (#11). The panel-style layout in Nord colours is #13. In the standalone app only, a Probe
// button opens the hardware discovery probe (#3).
class NL2XPluginEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit NL2XPluginEditor(NL2XEditorProcessor& owner);
    ~NL2XPluginEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void showProbe();
    void refreshGlobalChannelChoice();
    void globalChannelChosen();

    NL2XEditorProcessor& editorProcessor;

    ConnectionIndicator indicator;
    juce::TextButton checkButton { "Connection check" };
    juce::Label instanceWarning;
    juce::TextButton takeControlButton { "Take control" };

    juce::Label slotTitle { {}, "Slot A" };
    juce::Slider cutoffKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Label cutoffLabel;
    juce::AudioProcessorValueTreeState::SliderAttachment cutoffAttachment;

    juce::Label settingsTitle { {}, "MIDI settings" };
    MidiPortSelector portSelector;
    juce::Label globalChannelLabel { {}, "Global Channel" };
    juce::ComboBox globalChannelChoice;
    juce::TextButton probeButton { "Probe" };
    juce::Label synthTitle { {}, "Read from the synth" };
    juce::Label warningLabel;
    std::uint64_t shownRevision = 0;
    SelectorBar selectorBar;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    SlotView slotView;
    PerformanceView performanceView;
    ConnectionCheckView checkView;
    // The Library panel at the side (#11); it auditions on the Slot chosen in slotView.
    LibraryPanel libraryPanel;
    std::unique_ptr<ProbeWindow> probeWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NL2XPluginEditor)
};
