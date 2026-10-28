#pragma once

#include "LibraryPanel.h"
#include "MidiPortSelector.h"
#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

// The service settings (spec #1, GUI; user stories 1–5, 49), kept out of the panel and out of the
// DAW parameters: the MIDI ports, the Global Channel, the Library folder and project recall (#8). Shown over the main
// view by the Settings button; each setting is saved per user as soon as it changes. New settings
// get a row of their own here.
class SettingsPanel final : public juce::Component, private juce::Timer {
public:
    SettingsPanel(NL2XEditorProcessor& owner, LibraryPanel& library);
    ~SettingsPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override;

    std::function<void()> onClose;

private:
    void timerCallback() override;
    void refresh();
    void globalChannelChosen();

    NL2XEditorProcessor& processor;
    LibraryPanel& libraryPanel;

    juce::TextButton closeButton { "Close" };
    juce::Label midiTitle { {}, "MIDI" };
    MidiPortSelector portSelector;
    juce::Label globalChannelLabel { {}, "Global Channel" };
    juce::ComboBox globalChannelChoice;
    juce::Label globalChannelHint;
    juce::Label libraryTitle { {}, "Library" };
    juce::Label folderLabel;
    juce::TextButton chooseButton { juce::String::fromUTF8("Choose\xE2\x80\xA6") };
    juce::Label projectTitle { {}, "Project" };
    // Project recall (#8): per user, so the MidiPorts settings hold it.
    juce::ToggleButton sendProjectSoundsToggle { "Send the project's sounds to the synth when the project opens" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsPanel)
};
