#pragma once

#include "AboutPanel.h"
#include "ConnectionCheckView.h"
#include "ConnectionIndicator.h"
#include "LibrarianView.h"
#include "LibraryPanel.h"
#include "PerformanceView.h"
#include "PluginProcessor.h"
#include "ProbeWindow.h"
#include "SelectorBar.h"
#include "SettingsPanel.h"
#include "SlotView.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <functional>
#include <memory>

// Everything inside the editor window (docs/design/visual-style.md), laid out at one fixed design
// size; the window (NL2XPluginEditor) scales it as a whole when it is resized:
//
// - the header bar: the name (clicking it opens the About panel), the menu button (the window
//   sizes and About), the connection indicator (#9), Connection check, Undo and Redo (#14, also
//   Cmd+Z and Cmd+Shift+Z while the plugin window has keyboard focus), Settings, About, and in the
//   standalone app only the Probe button (#3);
// - tabs for Slots A–D (the front panel of the Slot's Program, SlotView), the Performance (#5) and
//   the Librarian (#44, which replaced the Synth memory tab of #12), with the Bank/Program and
//   Performance selectors (#10) and the Slot Channel warnings (or the single-instance warning)
//   above the Slot and Performance views;
// - the Library panel (#11): at the right-hand side, except in the Librarian tab, where it moves to
//   the left of the synth's Banks (the computer on the left, the synth on the right) and its sounds
//   are dragged onto Bank locations (this component is the drag-and-drop container);
// - over the tabs when open: the Connection check, the Settings panel (ports, Global Channel,
//   Library folder, project recall) or the About panel.
class EditorContent final : public juce::Component, public juce::DragAndDropContainer, private juce::Timer {
public:
    explicit EditorContent(NL2XEditorProcessor& owner);
    ~EditorContent() override;

    // The design size: the size at scale 1.
    static const int kWidth;
    static const int kHeight;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // The window's scale (1 = the design size) and a way to change it, given by the window that
    // holds this content (NL2XPluginEditor); the menu button offers the fixed sizes.
    std::function<double()> currentScale;
    std::function<void(double)> chooseScale;

private:
    // The tabs: Slots A–D, then these.
    static constexpr int kPerformanceTab = 4;
    static constexpr int kLibrarianTab = 5;

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

    // The name in the header bar, "NL2X EDITOR": a button that opens the About panel.
    class LogoButton final : public juce::Button {
    public:
        LogoButton();
        // The width the name needs.
        int preferredWidth() const;
        void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
    };

    // Three short lines: the menu with the window sizes and About.
    class MenuButton final : public juce::Button {
    public:
        MenuButton() : juce::Button("Menu") {}
        void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
    };

    void timerCallback() override;
    void showMenu();
    void showTab(int index);
    void showSettings(bool show);
    void showAbout(bool show);
    void showProbe();

    NL2XEditorProcessor& editorProcessor;

    LogoButton logoButton;
    MenuButton menuButton;
    ConnectionIndicator indicator;
    juce::TextButton checkButton { "Connection check" };
    juce::TextButton undoButton { "Undo" };
    juce::TextButton redoButton { "Redo" };
    juce::TextButton settingsButton { "Settings" };
    juce::TextButton aboutButton { "About" };
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
    // The Librarian tab (#44): the synth's Banks, composed from the Library; every write to the
    // synth's memory is confirmed there.
    LibrarianView librarianView;
    ConnectionCheckView checkView;
    SettingsPanel settingsPanel;
    AboutPanel aboutPanel;
    std::unique_ptr<ProbeWindow> probeWindow;
    juce::TooltipWindow tooltips { this };
    int shownTab = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EditorContent)
};
