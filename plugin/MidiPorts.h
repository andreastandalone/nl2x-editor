#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_data_structures/juce_data_structures.h>

#include <optional>

// The MIDI input and output ports connected to the synth, opened by the plugin itself rather than
// through the DAW (ADR 0001). The user's choice is remembered per user, not per project, in
// ~/Library/Application Support/NL2X Editor/, so it survives across projects.
//
// Message thread only. Listeners hear a change message whenever a port is chosen, appears or
// disappears.
class MidiPorts : public juce::ChangeBroadcaster {
public:
    // Incoming messages are delivered to `inputCallback` on the MIDI driver's thread. No port is
    // opened until open(): an instance that does not control the synth never opens one.
    explicit MidiPorts(juce::MidiInputCallback& inputCallback);

    // Opens the saved ports, and from then on reopens them when they come back.
    void open();
    // Closes both ports and stops reopening them; the saved choice is kept.
    void close();
    bool isOpen() const { return active; }

    // An empty identifier closes the port. The choice is saved. Ignored while closed.
    void selectInput(const juce::String& identifier);
    void selectOutput(const juce::String& identifier);

    juce::String inputIdentifier() const { return input != nullptr ? input->getIdentifier() : juce::String(); }
    juce::String outputIdentifier() const { return output != nullptr ? output->getIdentifier() : juce::String(); }

    // Ports that were chosen but cannot be opened (unplugged): reported to the user.
    juce::String missingInputName() const
    {
        return active && input == nullptr ? settings.getValue(kInputNameKey) : juce::String();
    }
    juce::String missingOutputName() const
    {
        return active && output == nullptr ? settings.getValue(kOutputNameKey) : juce::String();
    }

    void send(const juce::MidiMessage& message);

    // The Global Channel (SysEx Device ID, 0–15) the synth last answered on, kept with the ports
    // so that the next connection asks it first (spec #1, user story 5).
    std::optional<int> rememberedGlobalChannel() const;
    void rememberGlobalChannel(int channel);

    // The Global Channel (0–15) fixed by the user, or empty for Automatic, the default (spec #1,
    // user story 4). Per user, like the found one.
    std::optional<int> manualGlobalChannel() const;
    void setManualGlobalChannel(std::optional<int> channel);

    // Whether the Connection check has ever been shown to this user: it opens by itself on the
    // first connection ever.
    bool connectionCheckShown() const { return settings.getBoolValue(kConnectionCheckShownKey, false); }
    void setConnectionCheckShown();

    // The per-user settings file itself, for other per-user settings (the Library folder, #11).
    juce::PropertiesFile& userSettings() { return settings; }

private:
    static constexpr const char* kInputIdKey = "midiInputIdentifier";
    static constexpr const char* kInputNameKey = "midiInputName";
    static constexpr const char* kOutputIdKey = "midiOutputIdentifier";
    static constexpr const char* kOutputNameKey = "midiOutputName";
    static constexpr const char* kGlobalChannelKey = "globalChannel";
    static constexpr const char* kManualGlobalChannelKey = "manualGlobalChannel";
    static constexpr const char* kConnectionCheckShownKey = "connectionCheckShown";

    static juce::PropertiesFile::Options settingsOptions();

    // Closes ports that vanished and reopens saved ones that came back (interface replugged).
    void handleDeviceListChange();
    void reopenSavedPorts();

    juce::MidiInputCallback& inputCallback;
    juce::PropertiesFile settings;
    std::unique_ptr<juce::MidiInput> input;
    std::unique_ptr<juce::MidiOutput> output;
    juce::MidiDeviceListConnection deviceListConnection;
    bool active = false;
};
