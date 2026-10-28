#pragma once

#include "ConnectionCheckView.h"
#include "ConnectionIndicator.h"
#include "LibraryPanel.h"
#include "MemoryPanel.h"
#include "NordLookAndFeel.h"
#include "PerformanceView.h"
#include "PluginProcessor.h"
#include "ProbeWindow.h"
#include "SelectorBar.h"
#include "SettingsPanel.h"
#include "SlotView.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <cstdint>
#include <functional>
#include <memory>

// The editor window (docs/design/visual-style.md), fixed in size:
//
// - a red header bar: the connection indicator (#9), Connection check, Undo and Redo (#14, also
//   Cmd+Z and Cmd+Shift+Z while the plugin window has keyboard focus), Settings, and in the
//   standalone app only the Probe button (#3);
// - tabs for Slots A–D (the front panel of the Slot's Program, SlotView), the Performance (#5) and
//   the Synth memory (#12), with the Bank/Program and Performance selectors (#10) and the Slot
//   Channel warnings (or the single-instance warning) above the view;
// - the Library panel at the side (#11);
// - over the tabs when open: the Connection check, or the Settings panel (ports, Global Channel,
//   Library folder).
class NL2XPluginEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit NL2XPluginEditor(NL2XEditorProcessor& owner);
    ~NL2XPluginEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    // The tabs: Slots A–D, then these.
    static constexpr int kPerformanceTab = 4;
    static constexpr int kMemoryTab = 5;

    // A tab bar that reports the user's choice.
    class ViewTabs final : public juce::TabbedButtonBar {
    public:
        ViewTabs() : juce::TabbedButtonBar(juce::TabbedButtonBar::TabsAtTop) {}
        std::function<void(int)> onTabChosen;
        void currentTabChanged(int index, const juce::String&) override
        {
            if (onTabChosen)
                onTabChosen(index);
        }
    };

    void timerCallback() override;
    void showTab(int index);
    void showSettings(bool show);
    void showProbe();

    NL2XEditorProcessor& editorProcessor;
    // Before every component that uses it.
    NordLookAndFeel lookAndFeel;

    ConnectionIndicator indicator;
    juce::TextButton checkButton { "Connection check" };
    juce::TextButton undoButton { "Undo" };
    juce::TextButton redoButton { "Redo" };
    juce::TextButton settingsButton { "Settings" };
    juce::TextButton probeButton { "Probe" };

    ViewTabs tabs;
    SelectorBar selectorBar;
    juce::Label warningLabel;
    juce::Label instanceWarning;
    juce::TextButton takeControlButton { "Take control" };
    std::uint64_t shownRevision = 0;

    SlotView slotView;
    PerformanceView performanceView;
    // The Library panel at the side (#11); it auditions on the Slot shown in slotView.
    LibraryPanel libraryPanel;
    // The "Synth memory" tab (#12): writes to the synth's Banks, after confirmation.
    MemoryPanel memoryPanel;
    ConnectionCheckView checkView;
    SettingsPanel settingsPanel;
    std::unique_ptr<ProbeWindow> probeWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NL2XPluginEditor)
};
