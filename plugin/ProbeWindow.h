#pragma once

#include "MidiPortSelector.h"
#include "PluginProcessor.h"

#include "nl2x/probe/ProbeLog.h"
#include "nl2x/probe/ProbeSession.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <optional>

// The hardware discovery probe (#3), standalone app only: a thin window over
// nl2x::probe::ProbeSession, which holds every script. It shows and saves a log of all MIDI
// exchanged with the synth. Message thread only.
class ProbeComponent final : public juce::Component,
                             private NL2XEditorProcessor::MidiMonitor,
                             private juce::Timer {
public:
    explicit ProbeComponent(NL2XEditorProcessor& owner);
    ~ProbeComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class ProbeOutput final : public nl2x::MidiOutput {
    public:
        explicit ProbeOutput(NL2XEditorProcessor& owner) : processor(owner) {}
        void send(std::span<const std::uint8_t> message) override { processor.sendToSynth(message); }

    private:
        NL2XEditorProcessor& processor;
    };

    struct Field {
        juce::Label label;
        juce::TextEditor editor;
    };

    void midiFromSynth(std::span<const std::uint8_t> message, double timeMs) override;
    void midiToSynth(std::span<const std::uint8_t> message, double timeMs) override;
    void timerCallback() override;

    void runScript(nl2x::probe::ScriptId id);
    void startScript(nl2x::probe::ScriptId id, const nl2x::probe::ScriptOptions& options);
    std::optional<nl2x::probe::ScriptOptions> readOptions();
    bool applyGlobalChannel();
    void sendRawMessage();
    void saveLog();
    void saveBankBackup(const nl2x::probe::BankBackup& backup);

    void note(const juce::String& text);
    void appendLine(const std::string& line);
    void flushLog();
    void setUpField(Field& field, const juce::String& labelText, const juce::String& initialText);

    NL2XEditorProcessor& processor;
    ProbeOutput output { processor };
    nl2x::probe::ProbeLog log;
    nl2x::probe::ProbeSession session;

    MidiPortSelector portSelector;
    Field globalChannelField;
    Field sweepCcField;
    Field sweepChannelField;
    Field bankRequestTypeField;
    Field bankDumpTypeField;
    juce::OwnedArray<juce::TextButton> scriptButtons;
    juce::TextButton stopButton { "Stop" };
    juce::Label statusLabel;
    juce::TextEditor rawEntry;
    juce::TextButton sendButton { "Send" };
    juce::TextEditor logView;
    juce::TextButton clearButton { "Clear log" };
    juce::TextButton saveButton { "Save log" };
    juce::TooltipWindow tooltips { this };
    std::unique_ptr<juce::FileChooser> chooser;

    juce::String pendingLogText;
    double lastFlushMs = 0.0;
    nl2x::GlobalChannel shownGlobalChannel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProbeComponent)
};

class ProbeWindow final : public juce::DocumentWindow {
public:
    explicit ProbeWindow(NL2XEditorProcessor& owner);

    // Closing only hides the window: the probe session (and a Bank backup in it) lives on.
    void closeButtonPressed() override { setVisible(false); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProbeWindow)
};
