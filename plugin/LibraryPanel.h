#pragma once

#include "PluginProcessor.h"

#include "nl2x/Library.h"
#include "nl2x/LibraryMetadata.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

// The Library panel (#11) at the side of the editor: the Library folder (chosen in the Settings
// panel), the .syx files in it (Bank files open into numbered entries), Audition by clicking a Program or Percussion Kit
// (double-clicking a Performance, which replaces all four Slots), and saving the selected Slot,
// the Performance or a selected entry as a new .syx file. Refusals and results go to a status line.
// The user's own name, category and favourite flag for a sound (#39) are edited below the tree and
// kept in library.json by the engine (LibraryMetadata); the tree can be filtered by them. Each
// subfolder of the Library folder is a collapsible group named after it, nested as the folders are
// (#44); the open groups and files are remembered across rescans and editor reopens. Bank
// backups in the Backups subfolder are listed apart, under a closed "Backups" heading. A sound
// auditioned from the Library, edited in the editor and saved again keeps its information.
class LibraryPanel final : public juce::Component, private juce::Timer {
public:
    // `selectedSlot` names the Slot that Audition and "Save Slot" use: the Slot tab last shown.
    LibraryPanel(NL2XEditorProcessor& owner, std::function<nl2x::Slot()> selectedSlot);
    ~LibraryPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Called by the tree's items.
    void entryClicked(const nl2x::LibraryEntry& entry);
    void entryDoubleClicked(const nl2x::LibraryEntry& entry);
    void entrySelected(const nl2x::LibraryEntry* entry);
    void bankFileSelected(const nl2x::LibraryFile& file, bool isNowSelected);
    const nl2x::LibraryMetadata& metadata() const { return libraryMetadata; }

    // The selected file holding several dumps (a Bank file), for "Send Bank file to synth…" (#12);
    // nullptr when none is selected. Valid until the next rescan().
    const nl2x::LibraryFile* selectedBankFile() const { return selectedFile; }
    juce::File folder() const;
    // Reads the Library folder again.
    void rescan();
    // Asks for another Library folder (the Settings panel's "Choose…"), then shows it.
    void chooseFolder();

private:
    void timerCallback() override;
    void setFolder(const juce::File& newFolder);
    void auditionEntry(const nl2x::LibraryEntry& entry);
    void saveSlot();
    void savePerformance();
    void saveSelectedEntry();
    // Asks for a file name in the Library folder (the dialog asks before replacing a file), then
    // writes `bytes` there.
    // `place` names where the bytes come from ("Slot A", "Performance"): when the editor knows which
    // tagged sound is there (SoundLineage), the sound takes the saved values.
    void saveAs(const juce::String& title, const std::string& suggestedName, std::vector<std::uint8_t> bytes,
                const std::string& place = {});
    void setStatus(const juce::String& text);
    // Fills the tree from `files`: the groups and files (backups apart) when no filter is set,
    // otherwise the matching entries in their groups, open, empty groups left out.
    void rebuildTree();
    // Remembers which files of the unfiltered tree are open, to restore them after a filter or a
    // rescan. Call it while the tree's items are still valid.
    void keepOpenness();
    void refreshCategoryChoices();
    void filterChanged();
    // Shows the selected entry's information in the edit area.
    void showSelectedInfo();
    void commitName();
    void storeInfo(const nl2x::SoundInfo& info);

    NL2XEditorProcessor& processor;
    std::function<nl2x::Slot()> selectedSlot;

    juce::TextButton refreshButton { "Refresh" };
    juce::Label slotLabel;
    juce::ComboBox categoryFilter;
    juce::ToggleButton favouritesFilter { "Favourites only" };
    juce::TreeView tree;
    juce::Label infoTitle;
    juce::TextEditor nameEditor;
    juce::ComboBox categoryBox;
    juce::TextButton favouriteButton { "Favourite" };
    juce::TextButton saveSlotButton { "Save Slot as .syx" };
    juce::TextButton savePerformanceButton { "Save Performance as .syx" };
    juce::TextButton saveEntryButton { "Save entry as .syx" };
    juce::Label statusLabel;

    std::vector<nl2x::LibraryFile> files;
    nl2x::LibraryMetadata libraryMetadata;
    nl2x::LibraryFilter filter;
    nl2x::SoundLineage lineage;
    bool showingFiltered = false;
    std::unique_ptr<juce::XmlElement> unfilteredOpenness;
    std::vector<std::string> categories;
    std::unique_ptr<juce::TreeViewItem> root;
    const nl2x::LibraryEntry* selectedEntry = nullptr;
    const nl2x::LibraryFile* selectedFile = nullptr;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibraryPanel)
};
