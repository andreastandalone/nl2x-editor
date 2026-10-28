#include "PluginProcessor.h"

#include "PluginEditor.h"

#include <array>
#include <utility>

namespace {

// How often the shell polls parameters, delivers incoming MIDI and advances the engine's clock.
constexpr int kTimerIntervalMs = 10;

// The parameters exposed to the DAW. Grows with the engine's table (#6, #7).
constexpr std::array kDawParameters {
    std::pair { nl2x::Slot::A, nl2x::ProgramParameter::FilterCutoff },
};

nl2x::TimeMs nowMs()
{
    return static_cast<nl2x::TimeMs>(juce::Time::getMillisecondCounterHiRes());
}

int engineValue(const juce::RangedAudioParameter& parameter)
{
    return juce::roundToInt(parameter.convertFrom0to1(parameter.getValue()));
}

} // namespace

NL2XEditorProcessor::NL2XEditorProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "NL2XEditor", createParameterLayout()),
      ports(*this)
{
    for (const auto& [slot, parameter] : kDawParameters) {
        auto* dawParameter = parameters.getParameter(nl2x::dawParameterId(slot, parameter));
        jassert(dawParameter != nullptr);
        boundParameters.push_back({ slot, parameter, dawParameter, engineValue(*dawParameter) });
    }

    ports.addChangeListener(this);
    // The ports saved by the user are already open: connect at once (spec #1, user story 19).
    connectIfPortsChanged();
    startTimer(kTimerIntervalMs);
}

NL2XEditorProcessor::~NL2XEditorProcessor()
{
    stopTimer();
    ports.removeChangeListener(this);
}

void NL2XEditorProcessor::changeListenerCallback(juce::ChangeBroadcaster*)
{
    connectIfPortsChanged();
}

void NL2XEditorProcessor::connectIfPortsChanged()
{
    const auto input = ports.inputIdentifier();
    const auto output = ports.outputIdentifier();
    if (input.isEmpty() || output.isEmpty()) {
        // Forget the pair, so that replugging the interface connects again.
        connectedInput = {};
        connectedOutput = {};
        return;
    }
    if (input == connectedInput && output == connectedOutput)
        return;

    connectedInput = input;
    connectedOutput = output;
    std::optional<nl2x::GlobalChannel> remembered;
    if (const auto channel = ports.rememberedGlobalChannel())
        remembered = static_cast<nl2x::GlobalChannel>(*channel);
    engine.connect(remembered, nowMs());
}

void NL2XEditorProcessor::readSlotFromSynth(nl2x::Slot slot)
{
    engine.readSlot(slot, nowMs());
}

void NL2XEditorProcessor::rememberFoundGlobalChannel()
{
    if (const auto channel = engine.globalChannel())
        ports.rememberGlobalChannel(*channel);
}

juce::AudioProcessorValueTreeState::ParameterLayout NL2XEditorProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (const auto& [slot, parameter] : kDawParameters) {
        const auto& info = nl2x::programParameterInfo(parameter);
        const auto id = nl2x::dawParameterId(slot, parameter);
        layout.add(std::make_unique<juce::AudioParameterInt>(
            juce::ParameterID { id, 1 }, nl2x::dawParameterName(slot, parameter), info.minValue, info.maxValue,
            info.minValue));
    }
    return layout;
}

void NL2XEditorProcessor::prepareToPlay(double, int) {}

bool NL2XEditorProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto output = layouts.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::stereo() || output == juce::AudioChannelSet::mono();
}

void NL2XEditorProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Silent: the sound comes from the synth. Notes from the DAW track reach the synth through the
    // DAW's own MIDI routing, not through the plugin.
    buffer.clear();
}

juce::AudioProcessorEditor* NL2XEditorProcessor::createEditor()
{
    return new NL2XPluginEditor(*this);
}

void NL2XEditorProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void NL2XEditorProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes); xml != nullptr && xml->hasTagName(parameters.state.getType()))
        parameters.replaceState(juce::ValueTree::fromXml(*xml));
}

void NL2XEditorProcessor::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message)
{
    const std::scoped_lock lock(incomingLock);
    incoming.push_back(message);
}

void NL2XEditorProcessor::timerCallback()
{
    deliverIncomingMidi();
    sendChangedParameters();
    engine.advanceTime(nowMs());
    rememberFoundGlobalChannel();
}

void NL2XEditorProcessor::deliverIncomingMidi()
{
    std::vector<juce::MidiMessage> messages;
    {
        const std::scoped_lock lock(incomingLock);
        messages.swap(incoming);
    }

    for (const auto& message : messages) {
        const std::span<const std::uint8_t> bytes { message.getRawData(),
                                                    static_cast<std::size_t>(message.getRawDataSize()) };
        // JUCE stamps incoming messages in seconds on the same clock as getMillisecondCounterHiRes.
        const auto timeMs = message.getTimeStamp() * 1000.0;
        engine.handleIncomingMidi(bytes, static_cast<nl2x::TimeMs>(timeMs));
        if (midiMonitor != nullptr)
            midiMonitor->midiFromSynth(bytes, timeMs);
    }
}

void NL2XEditorProcessor::sendChangedParameters()
{
    // Polling instead of listening: the host may change parameters on any thread, the engine
    // must only be called here, and bursts of automation coalesce into one value per tick.
    for (auto& bound : boundParameters) {
        const auto value = engineValue(*bound.dawParameter);
        if (value != bound.lastSentValue) {
            bound.lastSentValue = value;
            engine.setParameter(bound.slot, bound.parameter, value);
        }
    }
}

void NL2XEditorProcessor::sendToSynth(std::span<const std::uint8_t> message)
{
    ports.send(juce::MidiMessage(message.data(), static_cast<int>(message.size())));
    if (midiMonitor != nullptr)
        midiMonitor->midiToSynth(message, juce::Time::getMillisecondCounterHiRes());
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NL2XEditorProcessor();
}
