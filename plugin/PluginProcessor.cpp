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

// The Performance settings that are DAW parameters (#5): useful to automate, unlike MIDI channels
// and global settings. The Slot is ignored for settings that are not per Slot.
constexpr std::array kDawPerformanceParameters {
    std::pair { nl2x::PerformanceSetting::KeyboardSplit, nl2x::Slot::A },
    std::pair { nl2x::PerformanceSetting::SplitPoint, nl2x::Slot::A },
    std::pair { nl2x::PerformanceSetting::Layer, nl2x::Slot::A },
    std::pair { nl2x::PerformanceSetting::Layer, nl2x::Slot::B },
    std::pair { nl2x::PerformanceSetting::Layer, nl2x::Slot::C },
    std::pair { nl2x::PerformanceSetting::Layer, nl2x::Slot::D },
};

// Plugin state attribute holding a Slot's Channel override: "slotChannelOverrideA" = 0–15.
juce::String slotChannelOverrideAttribute(nl2x::Slot slot)
{
    return "slotChannelOverride" + juce::String::charToString(nl2x::slotLetter(slot));
}

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
    for (const auto& [setting, slot] : kDawPerformanceParameters) {
        auto* dawParameter = parameters.getParameter(nl2x::performanceDawParameterId(setting, slot));
        jassert(dawParameter != nullptr);
        boundPerformanceParameters.push_back({ setting, slot, dawParameter, engineValue(*dawParameter) });
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

void NL2XEditorProcessor::readPerformanceFromSynth()
{
    engine.readPerformance(nowMs());
}

void NL2XEditorProcessor::editPerformanceSetting(nl2x::PerformanceSetting setting, nl2x::Slot slot, int value)
{
    const bool perSlot = nl2x::performanceSettingInfo(setting).perSlot;
    for (auto& bound : boundPerformanceParameters) {
        if (bound.setting != setting || (perSlot && bound.slot != slot))
            continue;
        // The timer polls the parameter and hands the value to the engine.
        bound.dawParameter->beginChangeGesture();
        bound.dawParameter->setValueNotifyingHost(bound.dawParameter->convertTo0to1(static_cast<float>(value)));
        bound.dawParameter->endChangeGesture();
        return;
    }
    engine.setPerformanceSetting(setting, slot, value);
}

void NL2XEditorProcessor::setSlotChannelOverride(nl2x::Slot slot, std::optional<nl2x::MidiChannel> channel)
{
    savedSlotChannelOverrides[nl2x::slotIndex(slot)] = channel ? static_cast<int>(*channel) : -1;
    engine.setSlotChannelOverride(slot, channel);
}

void NL2XEditorProcessor::applySavedSlotChannelOverrides()
{
    if (! slotChannelOverridesRestored.exchange(false))
        return;
    for (auto slot : nl2x::kAllSlots) {
        const int saved = savedSlotChannelOverrides[nl2x::slotIndex(slot)];
        engine.setSlotChannelOverride(slot, saved >= 0 && saved <= 15
                                                ? std::optional<nl2x::MidiChannel>(static_cast<nl2x::MidiChannel>(saved))
                                                : std::nullopt);
    }
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
    for (const auto& [setting, slot] : kDawPerformanceParameters) {
        const auto& info = nl2x::performanceSettingInfo(setting);
        const juce::ParameterID id { nl2x::performanceDawParameterId(setting, slot), 1 };
        const auto name = nl2x::performanceDawParameterName(setting, slot);
        if (info.minValue == 0 && info.maxValue == 1)
            layout.add(std::make_unique<juce::AudioParameterBool>(id, name, false));
        else
            layout.add(std::make_unique<juce::AudioParameterInt>(id, name, info.minValue, info.maxValue,
                                                                 (info.minValue + info.maxValue + 1) / 2));
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
    if (auto xml = parameters.copyState().createXml()) {
        for (auto slot : nl2x::kAllSlots)
            if (const int channel = savedSlotChannelOverrides[nl2x::slotIndex(slot)]; channel >= 0)
                xml->setAttribute(slotChannelOverrideAttribute(slot), channel);
        copyXmlToBinary(*xml, destData);
    }
}

void NL2XEditorProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName(parameters.state.getType()))
        return;
    for (auto slot : nl2x::kAllSlots) {
        const auto attribute = slotChannelOverrideAttribute(slot);
        savedSlotChannelOverrides[nl2x::slotIndex(slot)] = xml->getIntAttribute(attribute, -1);
        // Not a parameter: keep it out of the parameter tree.
        xml->removeAttribute(attribute);
    }
    slotChannelOverridesRestored = true;
    parameters.replaceState(juce::ValueTree::fromXml(*xml));
}

void NL2XEditorProcessor::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message)
{
    const std::scoped_lock lock(incomingLock);
    incoming.push_back(message);
}

void NL2XEditorProcessor::timerCallback()
{
    applySavedSlotChannelOverrides();
    deliverIncomingMidi();
    // DAW to engine first, so that a value the user just set is never overwritten by the older one
    // the engine still holds.
    sendChangedParameters();
    followPerformanceFromSynth();
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
    // Refused until the Performance has been read; the value read then replaces it.
    for (auto& bound : boundPerformanceParameters) {
        const auto value = engineValue(*bound.dawParameter);
        if (value != bound.lastSentValue) {
            bound.lastSentValue = value;
            engine.setPerformanceSetting(bound.setting, bound.slot, value);
        }
    }
}

void NL2XEditorProcessor::followPerformanceFromSynth()
{
    if (engine.stateRevision() == followedRevision)
        return;
    followedRevision = engine.stateRevision();

    const auto& performance = engine.performance();
    if (! performance)
        return;
    for (auto& bound : boundPerformanceParameters) {
        const auto value = performance->setting(bound.setting, bound.slot);
        if (value == engineValue(*bound.dawParameter))
            continue;
        bound.dawParameter->setValueNotifyingHost(bound.dawParameter->convertTo0to1(static_cast<float>(value)));
        // What the parameter now holds (a value outside its range is shown at the nearest end) counts
        // as sent, so polling does not send it back as an edit.
        bound.lastSentValue = engineValue(*bound.dawParameter);
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
