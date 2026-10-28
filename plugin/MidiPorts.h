#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_data_structures/juce_data_structures.h>

#include <functional>

// The MIDI input and output ports connected to the synth, opened by the plugin itself rather than
// through the DAW (ADR 0001). The user's choice is remembered per user, not per project, in
// ~/Library/Application Support/NL2X Editor/, so it survives across projects.
//
// Message thread only.
class MidiPorts {
public:
    // Incoming messages are delivered to `inputCallback` on the MIDI driver's thread.
    explicit MidiPorts(juce::MidiInputCallback& inputCallback);

    // An empty identifier closes the port. The choice is saved.
    void selectInput(const juce::String& identifier);
    void selectOutput(const juce::String& identifier);

    juce::String inputIdentifier() const { return input != nullptr ? input->getIdentifier() : juce::String(); }
    juce::String outputIdentifier() const { return output != nullptr ? output->getIdentifier() : juce::String(); }

    // Ports that were chosen but cannot be opened (unplugged): reported to the user.
    juce::String missingInputName() const { return input == nullptr ? settings.getValue(kInputNameKey) : juce::String(); }
    juce::String missingOutputName() const { return output == nullptr ? settings.getValue(kOutputNameKey) : juce::String(); }

    // Called on the message thread whenever ports appear or disappear.
    std::function<void()> onPortsChanged;

    void send(const juce::MidiMessage& message);

private:
    static constexpr const char* kInputIdKey = "midiInputIdentifier";
    static constexpr const char* kInputNameKey = "midiInputName";
    static constexpr const char* kOutputIdKey = "midiOutputIdentifier";
    static constexpr const char* kOutputNameKey = "midiOutputName";

    static juce::PropertiesFile::Options settingsOptions();

    // Closes ports that vanished and reopens saved ones that came back (interface replugged).
    void handleDeviceListChange();
    void reopenSavedPorts();

    juce::MidiInputCallback& inputCallback;
    juce::PropertiesFile settings;
    std::unique_ptr<juce::MidiInput> input;
    std::unique_ptr<juce::MidiOutput> output;
    juce::MidiDeviceListConnection deviceListConnection;
};
