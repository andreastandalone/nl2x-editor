#pragma once

#include "PluginProcessor.h"

#include "nl2x/CategorySuggestion.h"
#include "nl2x/Library.h"
#include "nl2x/LibraryMetadata.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
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
//
// A sound with no category of its own shows a suggested one (nl2x::suggestCategory) as a dashed
// tag such as "Bass?": clicking the tag keeps it as the sound's category, the sound's context menu
// keeps or dismisses it. A suggestion is never written to library.json until the user keeps it or
// runs Auto-tag; dismissed suggestions are remembered in the editor's settings. Suggestions learn
// from the categories the user set by hand (nl2x::LearnedCategories), relearned whenever one
// changes.
//
// The "Auto-tag…" button's menu keeps every current suggestion at once, after a confirmation that
// counts them by category, as auto-assigned categories (a solid but darker tag); "Undo auto-tag"
// removes those the user has not touched since. Picking or keeping a category by hand makes it
// the user's own.
//
// For the Librarian (#44): a Program can be dragged onto a Bank location and a Bank file onto a
// Bank (the drag description names it; draggedEntry() and draggedBankFile() resolve it), and a
// Program's context menu offers "Store to Bank/Program…" (onStoreEntry). The editor that holds
// the panel must be a juce::DragAndDropContainer.
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
    void entryMenu(const nl2x::LibraryEntry& entry);
    const nl2x::LibraryMetadata& metadata() const { return libraryMetadata; }
    // Counts the changes to the user's information and to the suggestions shown, for views that
    // show them (the Librarian) to know when to repaint.
    int metadataRevision() const { return metadataChanges; }

    // The category suggested for a sound: none when the user gave it a category or dismissed the
    // suggestion. A sound in the Library gets the most trusted suggestion of any entry holding it
    // (its own file's name before its parameters); any other Program is judged by its parameters.
    std::optional<nl2x::CategorySuggestion> suggestionFor(const nl2x::SoundFingerprint& fingerprint) const;
    // Keeps the suggestion as the sound's category, in library.json.
    void acceptSuggestion(const nl2x::SoundFingerprint& fingerprint);
    // Hides the suggestion for this sound from now on.
    void dismissSuggestion(const nl2x::SoundFingerprint& fingerprint);
    // Makes an auto-assigned category the user's own, or removes it.
    void keepAutoAssigned(const nl2x::SoundFingerprint& fingerprint);
    void removeAutoAssigned(const nl2x::SoundFingerprint& fingerprint);

    // The files as last scanned; valid until the next rescan(), whose count `contentsRevision()`
    // gives, so that views built from them know when to rebuild.
    const std::vector<nl2x::LibraryFile>& libraryFiles() const { return files; }
    int contentsRevision() const { return scanCount; }

    // Drag and drop to the Librarian: the description of a dragged entry or Bank file, and back.
    // Both resolve to nullptr for any other description, or after a rescan.
    juce::var dragDescriptionOf(const nl2x::LibraryEntry& entry) const;
    juce::var dragDescriptionOf(const nl2x::LibraryFile& file) const;
    const nl2x::LibraryEntry* draggedEntry(const juce::var& description) const;
    const nl2x::LibraryFile* draggedBankFile(const juce::var& description) const;

    // "Store to Bank/Program…" in a Program's context menu (the Librarian asks where).
    std::function<void(const nl2x::LibraryEntry&)> onStoreEntry;

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
    void storeInfo(const nl2x::SoundFingerprint& fingerprint, const nl2x::SoundInfo& info);
    // Learns again from the categories set by hand, then finds the suggestion for every sound in
    // the Library.
    void findSuggestions();
    // The "Auto-tag…" button: a menu with Auto-tag and Undo auto-tag, each confirmed first.
    void showAutoTagMenu();
    std::vector<nl2x::LibraryMetadata::AutoAssignment> autoTagPlan() const;
    void autoTag(const std::vector<nl2x::LibraryMetadata::AutoAssignment>& plan);
    void undoAutoTag();
    // After a change to many sounds' information: suggestions, choices and the tree again.
    void metadataReplaced(nl2x::MetadataSaveResult result);

    NL2XEditorProcessor& processor;
    std::function<nl2x::Slot()> selectedSlot;

    juce::TextButton refreshButton { "Refresh" };
    juce::TextButton autoTagButton { juce::String::fromUTF8("Auto-tag\xE2\x80\xA6") };
    juce::DrawableButton finderButton { "Finder", juce::DrawableButton::ImageOnButtonBackground };
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
    int scanCount = 0;
    nl2x::LibraryMetadata libraryMetadata;
    nl2x::LibraryFilter filter;
    nl2x::SoundLineage lineage;
    int metadataChanges = 0;
    // What the categories set by hand teach, as of the last findSuggestions().
    nl2x::LearnedCategories learned;
    // By fingerprint key: the suggestion for each sound in the Library.
    std::unordered_map<std::string, nl2x::CategorySuggestion> suggestions;
    // By fingerprint key: the suggestion for a Program not in the Library (a Bank location), kept
    // until the next findSuggestions() so that painting does not ask the neighbours again.
    mutable std::unordered_map<std::string, std::optional<nl2x::CategorySuggestion>> otherSuggestions;
    // Fingerprint keys of the sounds whose suggestion the user dismissed.
    std::set<std::string> dismissedSuggestions;
    bool showingFiltered = false;
    std::unique_ptr<juce::XmlElement> unfilteredOpenness;
    std::vector<std::string> categories;
    std::unique_ptr<juce::TreeViewItem> root;
    const nl2x::LibraryEntry* selectedEntry = nullptr;
    const nl2x::LibraryFile* selectedFile = nullptr;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibraryPanel)
};
