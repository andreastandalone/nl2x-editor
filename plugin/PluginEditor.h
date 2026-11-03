#pragma once

#include "EditorContent.h"
#include "NordLookAndFeel.h"
#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

// The editor window. Its content (EditorContent) is laid out once, at a fixed design size, and
// scaled as a whole by an affine transform: resizing the window by its corner keeps the aspect
// ratio and scales every control, font and line together, in the plugin hosts and the standalone
// app alike, without any component having to lay itself out for another size. Popup menus and
// tooltips follow the scale of the component they belong to.
//
// The menu button in the header bar sets the fixed sizes (75 % to 200 %) through the same path:
// the editor's size changes, the host or the standalone window follows it, and the scale is stored.
//
// The chosen scale is kept in the user settings (shared by every instance and the standalone app),
// so a new editor opens at the size last chosen. Cmd+Z and Cmd+Shift+Z (#14) reach keyPressed when
// the host lets the plugin window have the keyboard.
class NL2XPluginEditor final : public juce::AudioProcessorEditor {
public:
    explicit NL2XPluginEditor(NL2XEditorProcessor& owner);
    ~NL2XPluginEditor() override;

    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    // Resizes the editor to `scale` times the design size.
    void setScale(double scale);

    NL2XEditorProcessor& editorProcessor;
    // Before every component that uses it.
    NordLookAndFeel lookAndFeel;
    EditorContent content;
    // Off while the constructor sets the size up, so only the user's resizing is stored.
    bool storesScale = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NL2XPluginEditor)
};
