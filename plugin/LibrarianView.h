#pragma once

#include "LibraryPanel.h"
#include "PluginProcessor.h"

#include "nl2x/Librarian.h"
#include "nl2x/MemoryWriter.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>

// The synth side of the Librarian tab (#44): the four RAM Program Banks, one shown at a time as a
// grid of its 99 Programs, each with its location, the user's name and category (Library metadata,
// by fingerprint) or the name the Library gives the same sound, and pending locations marked "to
// send". The Library panel sits on its left while the tab is shown (EditorContent).
//
// - The first time the tab is shown the synth's Banks are read (Librarian::readSynthBanks);
//   "Read again" reads them again.
// - Dropping a Library Program on a location puts it there; dragging a location onto another
//   copies it; dropping a Program Bank file on a Bank tab or the grid replaces the Composed Bank
//   with it. A location's context menu undoes its change; "Discard changes" drops all of a Bank's.
// - Clicking a location auditions its Program (pending ones too) on the selected Slot. A sound
//   with no category shows the Library's suggested one as a dashed tag ("Bass?"): clicking the tag
//   keeps it, the context menu keeps or dismisses it.
// - "Send Bank N to synth…" writes the pending locations, "Save Bank as .syx…" writes the
//   Composed Bank into the Library folder; a single Library Program or the Slot being edited is
//   stored with askWhereToStore() and askWhereToStoreSlot().
// Every write is a MemoryWriter plan: confirmed here in an AlertWindow (and, without a backup, in a
// second one), with progress and Cancel below the grid.
class LibrarianView final : public juce::Component, private juce::Timer {
public:
    LibrarianView(NL2XEditorProcessor& owner, LibraryPanel& library, std::function<nl2x::Slot()> selectedSlot);
    ~LibrarianView() override;

    // The tab was shown: the first time, the synth's Banks are read as soon as the synth answers.
    void tabShown();
    void showBank(int bank);
    int shownBank() const { return bank; }

    // Asks for a Bank and Program, then stores the Slot's Program (Store… in the Slot tabs) or a
    // Library Program there, after the usual backup and confirmation.
    void askWhereToStoreSlot(nl2x::Slot slot);
    void askWhereToStore(const nl2x::LibraryEntry& entry);

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Called by the grid and the Bank tabs.
    void dropped(const juce::var& description, std::optional<nl2x::BankLocation> location, int targetBank);
    bool accepts(const juce::var& description, bool onBankTab) const;
    void locationClicked(nl2x::BankLocation location);
    void locationMenu(nl2x::BankLocation location);
    // Keeps the suggested category of the Program at `location` (a click on its dashed tag).
    void acceptSuggestion(nl2x::BankLocation location);

    // What a location shows.
    struct LocationText {
        // The user's name, else the name the Library gives the same sound; empty when unknown.
        juce::String name;
        bool userName = false;
        juce::String category;
        // A suggested category, shown only when the sound has none of its own.
        juce::String suggested;
        bool favourite = false;
        // Recognised by similarity: an edited version of a tagged sound.
        bool changed = false;
        bool known = false;
    };
    LocationText describe(nl2x::BankLocation location) const;
    LocationText describeSynth(nl2x::BankLocation location) const;
    juce::String tooltipFor(nl2x::BankLocation location) const;
    nl2x::Slot auditionSlot() const { return selectedSlot(); }

private:
    class BankTab;
    class BankGrid;

    void timerCallback() override;
    void refresh();
    void rebuildNames();
    LocationText describeProgram(const std::optional<nl2x::Program>& program) const;
    void readBanks();
    void send();
    void showReplaceMenu();
    void replaceWith(const nl2x::LibraryFile& file, int targetBank);
    void saveBankFile();
    void setMessage(const juce::String& text);
    void showLibrarianRefusal(nl2x::LibrarianRefusal refusal);
    void showMemoryRefusal(nl2x::MemoryRefusal refusal);
    // The Bank/Program dialog; `store` gets the chosen location.
    void askForLocation(const juce::String& title, const juce::String& message,
                        std::function<void(nl2x::BankLocation)> store);
    void askForConfirmation(const nl2x::MemoryWritePlan& plan);
    void askForNoBackupConsent(const nl2x::MemoryWritePlan& plan);
    void confirm(std::uint64_t token, nl2x::NoBackupConsent consent);

    NL2XEditorProcessor& processor;
    LibraryPanel& libraryPanel;
    std::function<nl2x::Slot()> selectedSlot;
    int bank = 0;

    std::array<std::unique_ptr<BankTab>, 4> bankTabs;
    juce::Label pendingLabel;
    juce::Label hintLabel;
    juce::TextButton readButton { "Read again" };
    std::unique_ptr<BankGrid> grid;
    juce::TextButton sendButton;
    juce::TextButton discardButton { "Discard changes" };
    juce::TextButton replaceButton { juce::String::fromUTF8("Replace with Bank file\xE2\x80\xA6") };
    juce::TextButton saveButton { juce::String::fromUTF8("Save Bank as .syx\xE2\x80\xA6") };
    juce::TextButton cancelButton { "Cancel" };
    juce::Label statusLabel;
    juce::Rectangle<int> progressArea;
    double progress = -1.0;

    // The name the Library gives each sound, by its fingerprint's values (first file found).
    std::map<std::string, std::string> libraryNames;
    int namesRevision = -1;
    int metadataRevision = -1;
    std::uint64_t shownRevision = 0;
    bool wantsFirstRead = false;
    bool wasBusy = false;
    // The plan whose question is shown (or was answered), so that each plan is asked once.
    std::uint64_t askedToken = 0;
    juce::String message;
    int lastStoreProgram = 1;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibrarianView)
};
