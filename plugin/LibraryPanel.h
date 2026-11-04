#pragma once

#include "PluginProcessor.h"
#include "TagChips.h"

#include "nl2x/Librarian.h"
#include "nl2x/Library.h"
#include "nl2x/LibraryMetadata.h"
#include "nl2x/TagSuggestion.h"

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
// Each subfolder of the Library folder is a collapsible group named after it, nested as the folders
// are (#44); the open groups and files are remembered across rescans and editor reopens. Bank
// backups in the Backups subfolder are listed apart, under a closed "Backups" heading. A sound
// auditioned from the Library, edited in the editor and saved again keeps its information.
//
// The user's own name, tags and favourite flag for a sound (#39) are kept in library.json by the
// engine (LibraryMetadata), and the tree can be filtered by a tag and by favourites. The edit area
// below the tree edits the sound selected last: a Library entry, or a location of the Librarian's
// Synth Banks (editLocation), named in its title. A sound's tags show as chips (TagChipRow) on its
// row, on its Librarian location and in the edit area, wherever they are:
// - clicking a tag offers to keep it (when Auto-tag gave it), replace it or remove it;
// - clicking the empty place after the tags, or "+N", offers to add one: the default tags, the
//   user's own, or a new one typed in;
// - a suggested tag (nl2x::suggestTags) is a dashed chip such as "Bass?": clicking it keeps it,
//   the sound's context menu keeps or dismisses it. A suggestion is never written to library.json
//   until the user keeps it or runs Auto-tag; dismissed suggestions are remembered in the editor's
//   settings. Suggestions learn from the tags the user set by hand (nl2x::LearnedTags), relearned
//   whenever one changes.
//
// The "Auto-tag…" button's menu adds every current suggestion at once, after a confirmation that
// counts them by tag, as auto-assigned tags (a solid but darker chip); "Undo auto-tag" removes
// those the user has not confirmed since. Keeping or replacing a tag by hand makes it the user's.
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

    // The edit area edits the sound at a location of the synth's Banks (as the Librarian shows it),
    // until a Library entry is selected.
    void editLocation(nl2x::BankLocation location);
    // The location being edited; empty while a Library entry (or nothing) is.
    std::optional<nl2x::BankLocation> editedLocation() const;

    // What a sound's tag row shows: its tags (the user's and Auto-tag's), then its suggested tags,
    // then "changed" when it was recognised as an edited version of a tagged sound.
    std::vector<TagChip> chipsFor(const nl2x::SoundFingerprint& fingerprint) const;
    // A click on a tag row: on a tag, on a suggested tag, or on the add zone or "+N".
    void tagClicked(const nl2x::SoundFingerprint& fingerprint, const TagChipRow::Hit& hit);

    // The tags suggested for a sound, leaving out those it has and those the user dismissed. A
    // sound in the Library gets the most trusted suggestions of any entry holding it (its own
    // file's name before its parameters); any other Program is judged by its parameters.
    std::vector<nl2x::TagSuggestion> suggestionsFor(const nl2x::SoundFingerprint& fingerprint) const;
    // Keeps a suggested tag as the sound's own, in library.json.
    void acceptSuggestion(const nl2x::SoundFingerprint& fingerprint, const std::string& tag);
    // Hides a suggested tag for this sound from now on.
    void dismissSuggestion(const nl2x::SoundFingerprint& fingerprint, const std::string& tag);

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
    class TagStrip;

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
    void refreshTagChoices();
    void filterChanged();

    // The sound the edit area edits, its title and the name the Library gives it; empty when there
    // is none (nothing selected, or a location not read yet).
    std::optional<nl2x::SoundFingerprint> editedSound() const;
    juce::String editedTitle() const;
    std::string editedDefaultName() const;
    // The name the Library gives a sound: that of a file holding it alone, else of a Bank entry.
    std::string libraryNameOf(const nl2x::SoundFingerprint& fingerprint) const;
    // Shows the edited sound's information in the edit area.
    void showSelectedInfo();
    void commitName();
    void storeInfo(const nl2x::SoundFingerprint& fingerprint, const nl2x::SoundInfo& info);

    // The menus of a tag row: for one tag, and for adding one (with the sound's tags in a submenu).
    void showTagMenu(const nl2x::SoundFingerprint& fingerprint, const std::string& tag);
    void showAddTagMenu(const nl2x::SoundFingerprint& fingerprint);
    // The tags offered to add to a sound, or to put in the place of one: every choice it does not
    // have, then "New tag…". `pick` gets the tag chosen.
    void addTagChoices(juce::PopupMenu& menu, const nl2x::SoundFingerprint& fingerprint,
                       std::function<void(LibraryPanel&, const std::string&)> pick);
    // Asks for a tag of the user's own, then gives it to `pick`.
    void askForNewTag(const juce::String& title, std::function<void(LibraryPanel&, const std::string&)> pick);
    // An existing choice spelled as the user typed it in another case is that choice.
    std::string choiceSpelling(const std::string& typed) const;
    void addTag(const nl2x::SoundFingerprint& fingerprint, const std::string& tag);
    void replaceTag(const nl2x::SoundFingerprint& fingerprint, const std::string& from, const std::string& to);
    void removeTag(const nl2x::SoundFingerprint& fingerprint, const std::string& tag);
    // Makes the sound's auto-assigned tags (or one of them) the user's own.
    void confirmTags(const nl2x::SoundFingerprint& fingerprint, const std::string& tag = {});
    void removeAutoAssigned(const nl2x::SoundFingerprint& fingerprint);

    // Learns again from the tags set by hand, then finds the suggestions for every sound in the
    // Library.
    void findSuggestions();
    // The "Auto-tag…" button: a menu with Auto-tag and Undo auto-tag, each confirmed first.
    void showAutoTagMenu();
    std::vector<nl2x::LibraryMetadata::AutoAssignment> autoTagPlan() const;
    void autoTag(const std::vector<nl2x::LibraryMetadata::AutoAssignment>& plan);
    void undoAutoTag();
    // After a change to many sounds' information: suggestions, choices and the tree again.
    void metadataReplaced(nl2x::MetadataSaveResult result);
    void showSaveResult(nl2x::MetadataSaveResult result);

    NL2XEditorProcessor& processor;
    std::function<nl2x::Slot()> selectedSlot;

    juce::TextButton refreshButton { "Refresh" };
    juce::TextButton autoTagButton { juce::String::fromUTF8("Auto-tag\xE2\x80\xA6") };
    juce::DrawableButton finderButton { "Finder", juce::DrawableButton::ImageOnButtonBackground };
    juce::Label slotLabel;
    juce::ComboBox tagFilter;
    juce::ToggleButton favouritesFilter { "Favourites only" };
    juce::TreeView tree;
    juce::Label infoTitle;
    juce::TextEditor nameEditor;
    std::unique_ptr<TagStrip> tagStrip;
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
    // What the tags set by hand teach, as of the last findSuggestions().
    nl2x::LearnedTags learned;
    // By fingerprint key: the suggestions for each sound in the Library.
    std::unordered_map<std::string, std::vector<nl2x::TagSuggestion>> suggestions;
    // By fingerprint key: the suggestions for a Program not in the Library (a Bank location), kept
    // until the next findSuggestions() so that painting does not ask the neighbours again.
    mutable std::unordered_map<std::string, std::vector<nl2x::TagSuggestion>> otherSuggestions;
    // Fingerprint key and tag (tab-separated) of each suggestion the user dismissed; a key alone
    // (from editors that suggested one category) dismisses every suggestion for that sound.
    std::set<std::string> dismissedSuggestions;
    bool showingFiltered = false;
    std::unique_ptr<juce::XmlElement> unfilteredOpenness;
    std::vector<std::string> tagChoices;
    std::unique_ptr<juce::TreeViewItem> root;
    const nl2x::LibraryEntry* selectedEntry = nullptr;
    const nl2x::LibraryFile* selectedFile = nullptr;
    // The Synth Banks location the edit area edits, while `editingLocation`; else selectedEntry.
    nl2x::BankLocation location { 0, 1 };
    bool editingLocation = false;
    // The sound the edit area shows, to notice when another one arrives at the edited location.
    std::optional<nl2x::SoundFingerprint> shownSound;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibraryPanel)
};
