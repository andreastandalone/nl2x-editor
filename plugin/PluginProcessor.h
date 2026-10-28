#pragma once

#include "MidiPorts.h"
#include "SynthControl.h"

#include "nl2x/Engine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
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
    // "Read from synth" for the Performance. Message thread only.
    void readPerformanceFromSynth();
    // The user changed a Performance setting in the GUI. Settings that are DAW parameters change
    // through their parameter (so the DAW records it); the others go to the engine directly.
    // Message thread only.
    void editPerformanceSetting(nl2x::PerformanceSetting setting, nl2x::Slot slot, int value);
    // Pins a Slot's channel regardless of the Performance (empty releases it). A service setting:
    // kept in the plugin state, never a DAW parameter. Message thread only.
    void setSlotChannelOverride(nl2x::Slot slot, std::optional<nl2x::MidiChannel> channel);

    // Single instance (user story 11): whether this instance controls the synth. One that does
    // not opens no MIDI port and sends nothing. Message thread only.
    bool controlsSynth() const { return control.holds(); }
    SynthControl::Refusal controlRefusal() const { return control.refusal(); }
    // The user asks to take over after the controlling instance went away; false if it has not.
    bool takeControlOfSynth();

    // Manual Global Channel (0–15), or empty for Automatic: saved per user, then connects again.
    void setManualGlobalChannel(std::optional<nl2x::GlobalChannel> channel);

    // The Connection check panel. It opens by itself on the first connection ever and when the
    // synth stops answering; open() also starts a check unless one is running. Message thread only.
    void openConnectionCheck();
    void closeConnectionCheck();
    bool connectionCheckOpen() const { return checkOpen; }
    void skipConnectionCheckStep();
    // Milliseconds on the engine's clock, for showing the check's countdown.
    static nl2x::TimeMs engineTimeMs();

    // The Probe window runs a script: the engine sends no request and takes no reply meanwhile.
    void setEngineRequestsSuspended(bool suspended);

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

    // A DAW parameter bound to a Performance setting.
    struct BoundPerformanceParameter {
        nl2x::PerformanceSetting setting;
        nl2x::Slot slot;
        juce::RangedAudioParameter* dawParameter;
        int lastSentValue;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message) override;
    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void sendChangedParameters();
    // Moves the Performance DAW parameters to the values the engine read from the synth, without
    // that move being sent back as an edit.
    void followPerformanceFromSynth();
    void applySavedSlotChannelOverrides();
    // Connects to the synth when both ports are open and differ from the last connected pair.
    void connectIfPortsChanged();
    void rememberFoundGlobalChannel();
    // Opens the Connection check when it should open by itself, and reconnects after a check that
    // found the synth answering.
    void watchConnection();
    void forceReconnect();

    // Before `ports`: control is released only after the ports are closed.
    SynthControl control;
    juce::AudioProcessorValueTreeState parameters;
    // Declared before `ports`, so they outlive the MIDI input that writes to them.
    std::mutex incomingLock;
    std::vector<juce::MidiMessage> incoming;
    MidiPorts ports;
    MidiMonitor* midiMonitor = nullptr;
    EngineMidiOutput engineOutput { *this };
    nl2x::Engine engine { engineOutput };
    std::vector<BoundParameter> boundParameters;
    std::vector<BoundPerformanceParameter> boundPerformanceParameters;
    std::uint64_t followedRevision = 0;
    // Slot Channel overrides as saved in the plugin state, −1 = none. Atomic because hosts may
    // save and restore state from other threads; the timer hands changes to the engine.
    std::array<std::atomic<int>, 4> savedSlotChannelOverrides { -1, -1, -1, -1 };
    std::atomic<bool> slotChannelOverridesRestored { false };
    juce::String connectedInput;
    juce::String connectedOutput;
    bool checkOpen = false;
    // The check was started by this shell and has not been seen finishing yet.
    bool checkAwaitingResult = false;
    // The last of Connected or NoAnswer seen, to notice the synth stop answering.
    nl2x::ConnectionStatus lastSettledStatus = nl2x::ConnectionStatus::NotConnected;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NL2XEditorProcessor)
};
