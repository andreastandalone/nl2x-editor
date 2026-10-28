#pragma once

#include "MidiPorts.h"

#include "nl2x/Engine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <cstdint>
#include <mutex>
#include <span>
#include <vector>

// The plugin shell. It owns threading and the MIDI ports; every decision is the engine's.
//
// Threads: the engine runs on the message thread only. DAW parameter values (which hosts may set
// from the audio thread) are polled by a timer on the message thread; MIDI from the synth is
// queued by the MIDI driver's thread and drained by the same timer.
class NL2XEditorProcessor final : public juce::AudioProcessor,
                                  private juce::MidiInputCallback,
                                  private juce::ChangeListener,
                                  private juce::Timer {
public:
    NL2XEditorProcessor();
    ~NL2XEditorProcessor() override;

    juce::AudioProcessorValueTreeState& parameterState() { return parameters; }
    MidiPorts& midiPorts() { return ports; }

    // The engine's view of the synth, for the GUI: Slot states, Global Channel, connection status.
    // Poll Engine::stateRevision() to notice changes. Message thread only.
    const nl2x::Engine& synth() const { return engine; }
    // "Read from synth" for one Slot. Message thread only.
    void readSlotFromSynth(nl2x::Slot slot);

    // Sees every MIDI message exchanged with the synth (the Probe window). Message thread only.
    class MidiMonitor {
    public:
        virtual ~MidiMonitor() = default;
        // `timeMs` is on the juce::Time::getMillisecondCounterHiRes() clock.
        virtual void midiFromSynth(std::span<const std::uint8_t> message, double timeMs) = 0;
        virtual void midiToSynth(std::span<const std::uint8_t> message, double timeMs) = 0;
    };
    void setMidiMonitor(MidiMonitor* monitor) { midiMonitor = monitor; }

    // Sends a message to the synth's MIDI output port. Message thread only.
    void sendToSynth(std::span<const std::uint8_t> message);
    // Hands queued messages from the synth to the engine and the monitor now, instead of on the
    // next 10 ms tick (the probe measures timing). Message thread only.
    void deliverIncomingMidi();

    // AudioProcessor
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

private:
    // Connects the engine's output to the chosen MIDI output port.
    class EngineMidiOutput final : public nl2x::MidiOutput {
    public:
        explicit EngineMidiOutput(NL2XEditorProcessor& owner) : processor(owner) {}
        void send(std::span<const std::uint8_t> message) override { processor.sendToSynth(message); }

    private:
        NL2XEditorProcessor& processor;
    };

    // A DAW parameter bound to one Slot's Program parameter.
    struct BoundParameter {
        nl2x::Slot slot;
        nl2x::ProgramParameter parameter;
        juce::RangedAudioParameter* dawParameter;
        int lastSentValue;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message) override;
    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void sendChangedParameters();
    // Connects to the synth when both ports are open and differ from the last connected pair.
    void connectIfPortsChanged();
    void rememberFoundGlobalChannel();

    juce::AudioProcessorValueTreeState parameters;
    // Declared before `ports`, so they outlive the MIDI input that writes to them.
    std::mutex incomingLock;
    std::vector<juce::MidiMessage> incoming;
    MidiPorts ports;
    MidiMonitor* midiMonitor = nullptr;
    EngineMidiOutput engineOutput { *this };
    nl2x::Engine engine { engineOutput };
    std::vector<BoundParameter> boundParameters;
    juce::String connectedInput;
    juce::String connectedOutput;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NL2XEditorProcessor)
};
