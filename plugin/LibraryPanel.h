#pragma once

#include "PluginProcessor.h"

#include "nl2x/Library.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

// The Library panel (#11), plain until #13: the Library folder with "Choose…", the .syx files in
// it (Bank files open into numbered entries), Audition by clicking a Program or Percussion Kit
// (double-clicking a Performance, which replaces all four Slots), and saving the selected Slot,
// the Performance or a selected entry as a new .syx file. Refusals and results go to a status line.
class LibraryPanel final : public juce::Component, private juce::Timer {
public:
    // `selectedSlot` names the Slot that Audition and "Save Slot" use: the Slots tab's choice.
    LibraryPanel(NL2XEditorProcessor& owner, std::function<nl2x::Slot()> selectedSlot);
    ~LibraryPanel() override;

    void resized() override;

    // Called by the tree's items.
    void entryClicked(const nl2x::LibraryEntry& entry);
    void entryDoubleClicked(const nl2x::LibraryEntry& entry);
    void entrySelected(const nl2x::LibraryEntry* entry);

private:
    void timerCallback() override;
    juce::File folder() const;
    void setFolder(const juce::File& newFolder);
    void chooseFolder();
    void rescan();
    void auditionEntry(const nl2x::LibraryEntry& entry);
    void saveSlot();
    void savePerformance();
    void saveSelectedEntry();
    // Asks for a file name in the Library folder (the dialog asks before replacing a file), then
    // writes `bytes` there.
    void saveAs(const juce::String& title, const std::string& suggestedName, std::vector<std::uint8_t> bytes);
    void setStatus(const juce::String& text);

    NL2XEditorProcessor& processor;
    std::function<nl2x::Slot()> selectedSlot;

    juce::Label title { {}, "Library" };
    juce::Label folderLabel;
    juce::TextButton chooseButton { juce::String::fromUTF8("Choose\xE2\x80\xA6") };
    juce::TextButton refreshButton { "Refresh" };
    juce::Label slotLabel;
    juce::TreeView tree;
    juce::TextButton saveSlotButton { "Save Slot as .syx" };
    juce::TextButton savePerformanceButton { "Save Performance as .syx" };
    juce::TextButton saveEntryButton { "Save entry as .syx" };
    juce::Label statusLabel;

    std::vector<nl2x::LibraryFile> files;
    std::unique_ptr<juce::TreeViewItem> root;
    const nl2x::LibraryEntry* selectedEntry = nullptr;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibraryPanel)
};
