#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "SavedSelection.h"

#include <array>
#include <utility>

namespace {

// How often the shell polls parameters, delivers incoming MIDI and advances the engine's clock.
constexpr int kTimerIntervalMs = 10;

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
    // Every CC parameter of every Slot (#6).
    for (auto slot : nl2x::kAllSlots)
        for (const auto& info : nl2x::ccParameters()) {
            const auto parameter = info.id;
            bindParameter(
                nl2x::dawParameterId(slot, parameter), [this, slot, parameter](int value) { engine.setParameter(slot, parameter, value); },
                [this, slot, parameter]() -> std::optional<int> {
                    const auto& program = engine.slotState(slot).program;
                    return program ? std::optional<int>(nl2x::ccParameterValue(*program, parameter)) : std::nullopt;
                });
        }
    // The Morph Amounts of every Slot (#7). They reach the synth as throttled Program dumps, so
    // automating them is slow by design (spec #1, user story 39); a Kit or unread Slot refuses them.
    for (auto slot : nl2x::kAllSlots)
        for (auto parameter : nl2x::morphableParameters())
            bindParameter(
                nl2x::morphDawParameterId(slot, parameter),
                [this, slot, parameter](int value) { engine.setMorphAmount(slot, parameter, value, nowMs()); },
                [this, slot, parameter]() -> std::optional<int> {
                    const auto& program = engine.slotState(slot).program;
                    return program ? std::optional<int>(nl2x::morphAmountValue(*program, parameter)) : std::nullopt;
                });
    // Refused by the engine until the Performance has been read; the value read then replaces it.
    for (const auto& dawParameter : kDawPerformanceParameters) {
        const auto [setting, slot] = dawParameter;
        bindParameter(
            nl2x::performanceDawParameterId(setting, slot),
            [this, setting, slot](int value) { engine.setPerformanceSetting(setting, slot, value); },
            [this, setting, slot]() -> std::optional<int> {
                const auto& performance = engine.performance();
                return performance ? std::optional<int>(performance->setting(setting, slot)) : std::nullopt;
            });
    }

    if (const auto manual = ports.manualGlobalChannel())
        engine.setManualGlobalChannel(static_cast<nl2x::GlobalChannel>(*manual));

    ports.addChangeListener(this);
    // Only the instance that controls the synth opens the ports; the saved ones open at once and
    // it connects (spec #1, user stories 11 and 19).
    if (control.tryAcquire())
        ports.open();
    connectIfPortsChanged();
    startTimer(kTimerIntervalMs);
}

NL2XEditorProcessor::~NL2XEditorProcessor()
{
    stopTimer();
    ports.removeChangeListener(this);
    ports.close();
    control.release();
}

bool NL2XEditorProcessor::takeControlOfSynth()
{
    if (! control.tryAcquire())
        return false;
    ports.open();
    forceReconnect();
    return true;
}

void NL2XEditorProcessor::forceReconnect()
{
    connectedInput = {};
    connectedOutput = {};
    connectIfPortsChanged();
}

void NL2XEditorProcessor::setManualGlobalChannel(std::optional<nl2x::GlobalChannel> channel)
{
    ports.setManualGlobalChannel(channel ? std::optional<int>(*channel) : std::nullopt);
    engine.setManualGlobalChannel(channel);
    forceReconnect();
}

nl2x::TimeMs NL2XEditorProcessor::engineTimeMs()
{
    return nowMs();
}

void NL2XEditorProcessor::openConnectionCheck()
{
    checkOpen = true;
    const auto state = engine.connectionCheck().state;
    if (state == nl2x::ConnectionCheckState::Running || state == nl2x::ConnectionCheckState::WaitingForUser)
        return;
    if (! controlsSynth() || ports.inputIdentifier().isEmpty() || ports.outputIdentifier().isEmpty())
        return;
    engine.startConnectionCheck(nowMs());
    checkAwaitingResult = engine.connectionCheck().state != nl2x::ConnectionCheckState::Idle;
}

void NL2XEditorProcessor::closeConnectionCheck()
{
    checkOpen = false;
    engine.cancelConnectionCheck();
    checkAwaitingResult = false;
}

void NL2XEditorProcessor::skipConnectionCheckStep()
{
    engine.skipConnectionCheckStep(nowMs());
}

void NL2XEditorProcessor::setEngineRequestsSuspended(bool suspended)
{
    engine.setRequestsSuspended(suspended, nowMs());
}

void NL2XEditorProcessor::watchConnection()
{
    using nl2x::ConnectionStatus;
    const auto status = engine.connectionStatus();
    if (status == ConnectionStatus::Connected || status == ConnectionStatus::NoAnswer) {
        if (! ports.connectionCheckShown()) {
            // The first connection ever (user story 9).
            ports.setConnectionCheckShown();
            openConnectionCheck();
        } else if (status == ConnectionStatus::NoAnswer && lastSettledStatus == ConnectionStatus::Connected) {
            // The synth stopped answering.
            openConnectionCheck();
        }
        lastSettledStatus = status;
    }

    const auto& report = engine.connectionCheck();
    if (checkAwaitingResult && report.state == nl2x::ConnectionCheckState::Finished) {
        checkAwaitingResult = false;
        // The check found the synth while the connection had given up: connect again, so the
        // indicator and the Slots catch up. Not with a wrong manual Global Channel, which would
        // only fail again.
        const auto manual = engine.manualGlobalChannel();
        if (status != ConnectionStatus::Connected && report.answeringGlobalChannel
            && (! manual || manual == report.answeringGlobalChannel)) {
            connectedInput = {};
            connectedOutput = {};
            lastSettledStatus = ConnectionStatus::NotConnected;
            engine.connect(report.answeringGlobalChannel, nowMs());
            connectedInput = ports.inputIdentifier();
            connectedOutput = ports.outputIdentifier();
        }
    }
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

nl2x::AuditionResult NL2XEditorProcessor::auditionFromLibrary(const nl2x::LibraryEntry& entry, nl2x::Slot slot)
{
    return nl2x::audition(engine, entry, slot, nowMs());
}

void NL2XEditorProcessor::readPerformanceFromSynth()
{
    engine.readPerformance(nowMs());
}

void NL2XEditorProcessor::editPerformanceSetting(nl2x::PerformanceSetting setting, nl2x::Slot slot, int value)
{
    const auto step = editStep();
    const bool perSlot = nl2x::performanceSettingInfo(setting).perSlot;
    for (const auto& [dawSetting, dawSlot] : kDawPerformanceParameters)
        if (dawSetting == setting && (! perSlot || dawSlot == slot)
            && editThroughDawParameter(nl2x::performanceDawParameterId(dawSetting, dawSlot), value, step))
            return;
    engine.beginUserEdit(step);
    engine.setPerformanceSetting(setting, slot, value);
    engine.endUserEdit();
}

void NL2XEditorProcessor::editSlotParameter(nl2x::Slot slot, nl2x::CcParameter parameter, int value)
{
    const bool bound = editThroughDawParameter(nl2x::dawParameterId(slot, parameter), value, editStep());
    jassert(bound);
    juce::ignoreUnused(bound);
}

void NL2XEditorProcessor::editSlotMorphAmount(nl2x::Slot slot, nl2x::ProgramParameter baseParameter, int amount)
{
    const bool bound = editThroughDawParameter(nl2x::morphDawParameterId(slot, baseParameter), amount, editStep());
    jassert(bound);
    juce::ignoreUnused(bound);
}

void NL2XEditorProcessor::beginEditGesture()
{
    gestureStep = engine.newEditStep();
}

void NL2XEditorProcessor::endEditGesture()
{
    gestureStep.reset();
}

nl2x::EditStepId NL2XEditorProcessor::editStep()
{
    return gestureStep ? *gestureStep : engine.newEditStep();
}

void NL2XEditorProcessor::undo()
{
    engine.undo(nowMs());
    followEngine();
}

void NL2XEditorProcessor::redo()
{
    engine.redo(nowMs());
    followEngine();
}

bool NL2XEditorProcessor::editThroughDawParameter(const juce::String& id, int value, nl2x::EditStepId step)
{
    auto* dawParameter = parameters.getParameter(id);
    if (dawParameter == nullptr)
        return false;
    dawParameter->beginChangeGesture();
    dawParameter->setValueNotifyingHost(dawParameter->convertTo0to1(static_cast<float>(value)));
    dawParameter->endChangeGesture();
    // At once rather than on the next poll: only here is it known to be the user's edit.
    for (auto& bound : boundParameters)
        if (bound.dawParameter == dawParameter)
            handToEngine(bound, step);
    return true;
}

void NL2XEditorProcessor::handToEngine(BoundParameter& bound, std::optional<nl2x::EditStepId> step)
{
    const auto value = engineValue(*bound.dawParameter);
    if (value == bound.lastDawValue)
        return;
    bound.lastDawValue = value;
    if (step)
        engine.beginUserEdit(*step);
    bound.toEngine(value);
    if (step)
        engine.endUserEdit();
    // The engine now holds the DAW's value (or refused it): nothing to follow back.
    bound.lastEngineValue = bound.fromEngine();
}

void NL2XEditorProcessor::bindParameter(const juce::String& id, std::function<void(int)> toEngine,
                                        std::function<std::optional<int>()> fromEngine)
{
    auto* dawParameter = parameters.getParameter(id);
    jassert(dawParameter != nullptr);
    boundParameters.push_back({ dawParameter, std::move(toEngine), std::move(fromEngine), engineValue(*dawParameter),
                                std::nullopt });
}

void NL2XEditorProcessor::setSlotChannelOverride(nl2x::Slot slot, std::optional<nl2x::MidiChannel> channel)
{
    savedSlotChannelOverrides[nl2x::slotIndex(slot)] = channel ? static_cast<int>(*channel) : -1;
    engine.setSlotChannelOverride(slot, channel);
}

nl2x::SelectionResult NL2XEditorProcessor::selectProgram(nl2x::Slot slot, nl2x::ProgramSelection selection)
{
    return engine.selectProgram(slot, selection, nowMs());
}

nl2x::SelectionResult NL2XEditorProcessor::selectPerformance(nl2x::PerformanceSelection selection)
{
    return engine.selectPerformance(selection, nowMs());
}

void NL2XEditorProcessor::syncSavedSelection()
{
    const std::scoped_lock lock(selectionLock);
    if (selectionRestored) {
        selectionRestored = false;
        // Shown only: nothing is sent to the synth (that is #8's job).
        engine.restoreSelection(savedSelection);
    }
    savedSelection = engine.selection();
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
    // A manual Global Channel is not a found one.
    if (engine.manualGlobalChannel())
        return;
    if (const auto channel = engine.globalChannel())
        ports.rememberGlobalChannel(*channel);
}

juce::AudioProcessorValueTreeState::ParameterLayout NL2XEditorProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (auto slot : nl2x::kAllSlots)
        for (const auto& info : nl2x::ccParameters()) {
            const juce::ParameterID id { nl2x::dawParameterId(slot, info.id), 1 };
            const auto name = nl2x::dawParameterName(slot, info.id);
            switch (info.kind) {
            case nl2x::CcParameterKind::Knob:
                layout.add(std::make_unique<juce::AudioParameterInt>(id, name, info.minValue, info.maxValue,
                                                                     info.minValue));
                break;
            case nl2x::CcParameterKind::Stepped: {
                juce::StringArray choices;
                for (const auto& choice : info.choices)
                    choices.add(juce::String(std::string(choice)));
                layout.add(std::make_unique<juce::AudioParameterChoice>(id, name, choices, 0));
                break;
            }
            case nl2x::CcParameterKind::Switch:
                layout.add(std::make_unique<juce::AudioParameterBool>(id, name, false));
                break;
            }
        }
    for (auto slot : nl2x::kAllSlots)
        for (auto parameter : nl2x::morphableParameters())
            layout.add(std::make_unique<juce::AudioParameterInt>(
                juce::ParameterID { nl2x::morphDawParameterId(slot, parameter), 1 },
                nl2x::morphDawParameterName(slot, parameter), nl2x::kMorphAmountMin, nl2x::kMorphAmountMax, 0));
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
        {
            const std::scoped_lock lock(selectionLock);
            saved_selection::write(*xml, savedSelection);
        }
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
    {
        const std::scoped_lock lock(selectionLock);
        savedSelection = saved_selection::take(*xml);
        selectionRestored = true;
    }
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
    syncSavedSelection();
    deliverIncomingMidi();
    // DAW to engine first, so that a value the user just set is never overwritten by the older one
    // the engine still holds.
    sendChangedParameters();
    followEngine();
    engine.advanceTime(nowMs());
    rememberFoundGlobalChannel();
    watchConnection();
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
    for (auto& bound : boundParameters)
        handToEngine(bound, std::nullopt);
}

void NL2XEditorProcessor::followEngine()
{
    if (engine.stateRevision() == followedRevision)
        return;
    followedRevision = engine.stateRevision();

    for (auto& bound : boundParameters) {
        const auto value = bound.fromEngine();
        if (! value || value == bound.lastEngineValue)
            continue;
        bound.lastEngineValue = value;
        if (*value != engineValue(*bound.dawParameter)) {
            // As one gesture, so that a DAW writing automation records the synth's knob.
            bound.dawParameter->beginChangeGesture();
            const auto shown = bound.dawParameter->getNormalisableRange().getRange().clipValue(static_cast<float>(*value));
            bound.dawParameter->setValueNotifyingHost(bound.dawParameter->convertTo0to1(shown));
            bound.dawParameter->endChangeGesture();
        }
        // What the parameter now holds (a value outside its range is shown at the nearest end)
        // counts as sent, so polling does not send it back as an edit.
        bound.lastDawValue = engineValue(*bound.dawParameter);
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
