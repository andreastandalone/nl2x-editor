#pragma once

#include "LibraryPanel.h"
#include "PluginProcessor.h"

#include "nl2x/MemoryWriter.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <functional>
#include <memory>

// The "Synth memory" tab (#12): store the selected Slot's Program into a Bank
// location, read a Bank into a .syx file, send the Bank file selected in the Library. Every write
// is the engine's MemoryWriter plan, confirmed in an AlertWindow (and, without a backup, in a
// second one); progress, the backup path and the outcome are shown below the buttons.
class MemoryPanel final : public juce::Component, private juce::Timer {
public:
    MemoryPanel(NL2XEditorProcessor& owner, LibraryPanel& library, std::function<nl2x::Slot()> selectedSlot);
    ~MemoryPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    int chosenBank() const;
    void store();
    void readBank();
    void sendBank();
    void showRefusal(nl2x::MemoryRefusal refusal);
    void askForConfirmation(const nl2x::MemoryWritePlan& plan);
    void askForNoBackupConsent(const nl2x::MemoryWritePlan& plan);
    void confirm(std::uint64_t token, nl2x::NoBackupConsent consent);

    NL2XEditorProcessor& processor;
    LibraryPanel& libraryPanel;
    std::function<nl2x::Slot()> selectedSlot;

    juce::Label lockLabel;
    juce::Label bankLabel { {}, "Bank" };
    juce::ComboBox bankChoice;
    juce::Label programLabel { {}, "Program" };
    juce::ComboBox programChoice;
    juce::TextButton storeButton;
    juce::TextButton readButton { juce::String::fromUTF8("Read Bank from synth\xE2\x80\xA6") };
    juce::TextButton sendButton { juce::String::fromUTF8("Send Bank file to synth\xE2\x80\xA6") };
    juce::TextButton cancelButton { "Cancel" };
    juce::Label progressLabel;
    juce::Label messageLabel;

    // The plan whose question is shown (or was answered), so that each plan is asked once.
    std::uint64_t askedToken = 0;
    juce::String refusalText;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MemoryPanel)
};
