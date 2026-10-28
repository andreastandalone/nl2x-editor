#include "ConnectionCheck.h"

#include "nl2x/CcParameters.h"
#include "nl2x/HardwareAssumptions.h"
#include "nl2x/ProgramParameters.h"

#include <algorithm>
#include <string>
#include <utility>

namespace nl2x {

bool ConnectionCheckReport::allPassed() const
{
    return std::all_of(results.begin(), results.end(),
                       [](const auto& result) { return result.outcome == ConnectionCheckOutcome::Passed; });
}

namespace {

constexpr SynthConnection::Owner kCheckOwner = 1;
constexpr GlobalChannel kGlobalChannelCount = 16;
constexpr std::uint8_t kControlChange = 0xB0;
// The CC reception test sends Filter Cutoff this far from its current value.
constexpr int kCutoffTestOffset = 32;

// The manual's instructions [OFF-2X p.66–67], quoted by several results.
const std::string kTurnOnC = "Turn on C: hold down SHIFT and press PROG/CTRL until the display shows C, then set it "
                             "to \"on\" with the UP/DOWN buttons (Shift + PROG/CTRL, C on).";
const std::string kOutCableFix = "Check the cable from your MIDI interface's output to the synth's MIDI IN, and that "
                                 "\"MIDI Output (to the synth)\" in the settings is that output.";
const std::string kReturnCableFix = "Connect the synth's MIDI OUT to your MIDI interface's input, and choose that "
                                    "input as \"MIDI Input (from the synth)\" in the settings.";

std::string shown(GlobalChannel channel)
{
    return std::to_string(channel + hardware::kShownGlobalChannelOfDev0);
}

std::string howToSeeGlobalChannel()
{
    return "hold down SHIFT and press MIDI CH, then press MIDI CH again: the display shows the Global MIDI Channel "
           "(G.x)";
}

bool isFromSynth(std::span<const std::uint8_t> message)
{
    // Channel messages and SysEx; real-time bytes (clock, active sensing) may come from the
    // interface itself and prove nothing about the cable.
    return ! message.empty() && message[0] >= 0x80 && (message[0] < 0xF0 || message[0] == kSysExStart);
}

// A CC from a panel control. Mod wheel, expression pedal and sustain are sent whatever the C
// setting says [OFF-2X p.77], so they prove nothing about it.
bool isPanelCc(std::span<const std::uint8_t> message)
{
    if (message.size() != 3 || (message[0] & 0xF0) != kControlChange)
        return false;
    const auto cc = message[1];
    return std::find(hardware::kControllersIgnoringControlChangeSetting.begin(),
                     hardware::kControllersIgnoringControlChangeSetting.end(), cc)
           == hardware::kControllersIgnoringControlChangeSetting.end();
}

bool isSlotEditBufferReply(std::span<const std::uint8_t> message)
{
    if (hardware::classifySlotEditBufferReply(message.size()) == hardware::SlotEditBufferReply::NotAReply)
        return false;
    const auto parsed = parseSysEx(message);
    return parsed.kind != SysExKind::NotNordLead && parsed.values.has_value();
}

std::optional<Program> programOf(std::span<const std::uint8_t> reply)
{
    if (hardware::classifySlotEditBufferReply(reply.size()) != hardware::SlotEditBufferReply::Program)
        return std::nullopt;
    if (const auto dump = parseProgramDump(reply))
        return dump->program;
    return std::nullopt;
}

std::string slotName(Slot slot)
{
    return std::string("Slot ") + slotLetter(slot);
}

} // namespace

ConnectionCheck::ConnectionCheck(SynthConnection& synthConnection, MidiOutput& midiOutput,
                                 std::function<void()> onChange)
    : connection(synthConnection),
      output(midiOutput),
      changed(std::move(onChange))
{
}

bool ConnectionCheck::isActive() const
{
    return currentReport.state == ConnectionCheckState::Running
           || currentReport.state == ConnectionCheckState::WaitingForUser;
}

void ConnectionCheck::start(const Setup& newSetup, TimeMs timeMs)
{
    cancel();
    ++run;
    setup = newSetup;

    currentReport = {};
    const std::array<const char*, kConnectionCheckItemCount> titles {
        "MIDI from the synth", "SysEx requests", "Global Channel", "CCs from the synth", "CCs to the synth"
    };
    for (std::size_t i = 0; i < titles.size(); ++i) {
        currentReport.results[i].item = static_cast<ConnectionCheckItem>(i);
        currentReport.results[i].title = titles[i];
    }
    currentReport.state = ConnectionCheckState::Running;

    candidates.clear();
    const auto add = [this](GlobalChannel channel) {
        if (channel < kGlobalChannelCount && std::find(candidates.begin(), candidates.end(), channel) == candidates.end())
            candidates.push_back(channel);
    };
    // The manual one first, but the others too: finding where the synth really answers is the
    // point of the "wrong Global Channel" diagnosis.
    if (setup.manualGlobalChannel)
        add(*setup.manualGlobalChannel);
    if (setup.knownGlobalChannel)
        add(*setup.knownGlobalChannel);
    add(hardware::kFactoryGlobalChannel);
    for (GlobalChannel channel = 0; channel < kGlobalChannelCount; ++channel)
        add(channel);

    candidateIndex = 0;
    midiSeen = false;
    ccSeen = false;
    readings = {};
    step = Step::FindingSynth;
    set(ConnectionCheckItem::SysExAnswered, ConnectionCheckOutcome::Running);
    set(ConnectionCheckItem::GlobalChannel, ConnectionCheckOutcome::Running);
    askNextCandidate(timeMs);
}

void ConnectionCheck::cancel()
{
    ++run;
    connection.cancel(kCheckOwner);
    step = Step::None;
    // A finished check keeps its results until the next start.
    if (isActive()) {
        currentReport = {};
        changed();
    }
}

void ConnectionCheck::request(std::vector<std::uint8_t> message, SynthConnection::Accepts accepts,
                              std::function<void(std::optional<std::span<const std::uint8_t>>, TimeMs)> done,
                              TimeMs timeMs)
{
    connection.enqueue(
        std::move(message), std::move(accepts),
        [this, thisRun = run, done = std::move(done)](auto reply, TimeMs doneMs) {
            if (thisRun == run)
                done(reply, doneMs);
        },
        timeMs, kCheckOwner);
}

void ConnectionCheck::askNextCandidate(TimeMs timeMs)
{
    const auto channel = candidates[candidateIndex];
    request(slotEditBufferRequest(channel, Slot::A), isSlotEditBufferReply,
            [this, channel](auto reply, TimeMs doneMs) {
                if (reply) {
                    synthFound(channel, *reply, doneMs);
                } else if (++candidateIndex < candidates.size()) {
                    askNextCandidate(doneMs);
                } else {
                    noSysExAnswer(doneMs);
                }
            },
            timeMs);
}

void ConnectionCheck::synthFound(GlobalChannel channel, std::span<const std::uint8_t> reply, TimeMs timeMs)
{
    currentReport.answeringGlobalChannel = channel;
    readings[slotIndex(Slot::A)].emplace(programOf(reply));

    set(ConnectionCheckItem::MidiFromSynth, ConnectionCheckOutcome::Passed, "MIDI arrives from the synth.");
    set(ConnectionCheckItem::SysExAnswered, ConnectionCheckOutcome::Passed, "The synth answers the editor's requests.");

    if (setup.manualGlobalChannel && *setup.manualGlobalChannel != channel) {
        set(ConnectionCheckItem::GlobalChannel, ConnectionCheckOutcome::Failed,
            "The editor is set to Global Channel " + shown(*setup.manualGlobalChannel)
                + ", but the synth answers on Global Channel " + shown(channel) + ".",
            "Set the Global Channel in the editor's settings to Automatic or to " + shown(channel)
                + ". Or change it on the synth: " + howToSeeGlobalChannel() + "; it reads G." + shown(channel)
                + ". Set it to " + shown(*setup.manualGlobalChannel) + " with the UP/DOWN buttons.");
    } else {
        set(ConnectionCheckItem::GlobalChannel, ConnectionCheckOutcome::Passed,
            "The synth answers on Global Channel " + shown(channel) + ". To see it on the synth, "
                + howToSeeGlobalChannel() + "; it reads G." + shown(channel) + ".");
    }

    startAllControllers(timeMs);
}

void ConnectionCheck::noSysExAnswer(TimeMs timeMs)
{
    if (midiSeen) {
        midiButNoSysEx();
        return;
    }
    waitForUser(Step::WaitingForAnyMidi,
                "The synth did not answer. Play a key on the synth now (or turn a knob), so that the editor can "
                "tell whether anything arrives from it.",
                timeMs);
}

void ConnectionCheck::midiButNoSysEx()
{
    set(ConnectionCheckItem::MidiFromSynth, ConnectionCheckOutcome::Passed, "MIDI arrives from the synth.");
    set(ConnectionCheckItem::SysExAnswered, ConnectionCheckOutcome::Failed,
        "MIDI arrives from the synth, but it did not answer the editor's SysEx requests on any of the 16 Global "
        "Channels. Either the requests do not reach the synth, or it ignores them.",
        kOutCableFix + " Then run the check again.");
    setRemaining(ConnectionCheckOutcome::NotTested, "Not tested: the synth does not answer SysEx requests.");
    finish();
}

void ConnectionCheck::noMidiAtAll()
{
    set(ConnectionCheckItem::MidiFromSynth, ConnectionCheckOutcome::Failed,
        "Nothing arrived from the synth: no answer to the editor's requests, and no key or knob. The MIDI return "
        "cable seems to be missing, or the wrong MIDI input is chosen.",
        kReturnCableFix);
    setRemaining(ConnectionCheckOutcome::NotTested, "Not tested: nothing arrives from the synth.");
    finish();
}

void ConnectionCheck::startAllControllers(TimeMs timeMs)
{
    step = Step::AllControllers;
    ccSeen = false;
    set(ConnectionCheckItem::CcsTransmitted, ConnectionCheckOutcome::Running);
    request(allControllersRequest(*currentReport.answeringGlobalChannel, Slot::A),
            [](auto message) { return isPanelCc(message); },
            [this](auto reply, TimeMs doneMs) {
                if (reply || ccSeen)
                    ccsTransmitted("The synth sends CCs: it answered the All Controllers Request.", doneMs);
                else
                    waitForUser(Step::WaitingForKnob,
                                "No CCs came from the synth. Turn a knob on the synth now, for example the Filter "
                                "FREQUENCY knob.",
                                doneMs);
            },
            timeMs);
}

void ConnectionCheck::ccsTransmitted(const std::string& explanation, TimeMs timeMs)
{
    set(ConnectionCheckItem::CcsTransmitted, ConnectionCheckOutcome::Passed, explanation);
    startCcReceive(timeMs);
}

void ConnectionCheck::ccsNotTransmitted(TimeMs timeMs)
{
    set(ConnectionCheckItem::CcsTransmitted, ConnectionCheckOutcome::Failed,
        "No CC arrived from the synth: not in answer to the All Controllers Request, and not when you turned a "
        "knob. Turning a knob on the synth will not move it in the editor.",
        kTurnOnC);
    startCcReceive(timeMs);
}

void ConnectionCheck::startCcReceive(TimeMs timeMs)
{
    step = Step::ReadingSlot;
    set(ConnectionCheckItem::CcsReceived, ConnectionCheckOutcome::Running);
    slotUnderTest = 0;
    testNextSlot(timeMs);
}

void ConnectionCheck::testNextSlot(TimeMs timeMs)
{
    for (; slotUnderTest < kAllSlots.size(); ++slotUnderTest) {
        const auto slot = kAllSlots[slotUnderTest];
        if (! setup.slotChannels[slotIndex(slot)])
            continue;
        if (const auto& reading = readings[slotIndex(slot)]) {
            if (*reading) {
                testSlot(slot, *reading, timeMs);
                return;
            }
            continue;
        }
        // Not read yet: read it, then look again.
        step = Step::ReadingSlot;
        request(slotEditBufferRequest(*currentReport.answeringGlobalChannel, slot), isSlotEditBufferReply,
                [this, slot](auto reply, TimeMs doneMs) {
                    if (! reply) {
                        set(ConnectionCheckItem::CcsReceived, ConnectionCheckOutcome::NotTested,
                            "Not tested: the synth stopped answering.", "Run the check again.");
                        finish();
                        return;
                    }
                    readings[slotIndex(slot)].emplace(programOf(*reply));
                    testNextSlot(doneMs);
                },
                timeMs);
        return;
    }

    set(ConnectionCheckItem::CcsReceived, ConnectionCheckOutcome::NotTested,
        "Not tested: no Slot plays a Program on a MIDI channel (the test changes a Program's Filter Cutoff and puts "
        "it back).",
        "Select a Program (not a Percussion Kit) on Slot A and run the check again.");
    finish();
}

void ConnectionCheck::testSlot(Slot slot, const SlotReading& reading, TimeMs timeMs)
{
    const auto value = reading->value(ProgramParameter::FilterCutoff);
    if (value < 0 || value > 127) {
        // A CC cannot carry it back: leave this Slot alone.
        ++slotUnderTest;
        testNextSlot(timeMs);
        return;
    }

    originalCutoff = value;
    const auto testValue = value < 64 ? value + kCutoffTestOffset : value - kCutoffTestOffset;
    const auto channel = *setup.slotChannels[slotIndex(slot)];
    const std::array<std::uint8_t, 3> cc { static_cast<std::uint8_t>(kControlChange | channel),
                                           ccParameterInfo(CcParameter::FilterCutoff).cc,
                                           static_cast<std::uint8_t>(testValue) };
    output.send(cc);
    step = Step::SettlingCc;
    settleUntil = timeMs + hardware::kControlChangeSettleMs;
}

void ConnectionCheck::readBack(TimeMs timeMs)
{
    step = Step::ReadingBack;
    const auto slot = kAllSlots[slotUnderTest];
    request(slotEditBufferRequest(*currentReport.answeringGlobalChannel, slot), isSlotEditBufferReply,
            [this, slot](auto reply, TimeMs) {
                const auto program = reply ? programOf(*reply) : std::nullopt;
                if (! program) {
                    set(ConnectionCheckItem::CcsReceived, ConnectionCheckOutcome::NotTested,
                        "Not tested: the synth did not answer when " + slotName(slot) + " was read back.",
                        "Run the check again.");
                    finish();
                    return;
                }

                const auto channel = *setup.slotChannels[slotIndex(slot)];
                const auto shownChannel = std::to_string(channel + 1);
                if (program->value(ProgramParameter::FilterCutoff) != originalCutoff) {
                    // Put the Slot back as it was.
                    const std::array<std::uint8_t, 3> cc { static_cast<std::uint8_t>(kControlChange | channel),
                                                           ccParameterInfo(CcParameter::FilterCutoff).cc,
                                                           static_cast<std::uint8_t>(originalCutoff) };
                    output.send(cc);
                    set(ConnectionCheckItem::CcsReceived, ConnectionCheckOutcome::Passed,
                        "The synth receives CCs: " + slotName(slot) + " followed a Filter Cutoff change sent on MIDI "
                            "channel " + shownChannel + ", and was put back.");
                } else {
                    set(ConnectionCheckItem::CcsReceived, ConnectionCheckOutcome::Failed,
                        "The editor sent Filter Cutoff to " + slotName(slot) + " on MIDI channel " + shownChannel
                            + ", and the Slot did not change. Knobs turned in the editor will not be heard.",
                        kTurnOnC + " If C is already on, check " + slotName(slot) + "'s MIDI channel: hold down SHIFT, "
                            "press MIDI CH and select " + slotName(slot) + "; the editor expects channel " + shownChannel
                            + ".");
                }
                finish();
            },
            timeMs);
}

void ConnectionCheck::waitForUser(Step newStep, std::string prompt, TimeMs timeMs)
{
    step = newStep;
    currentReport.state = ConnectionCheckState::WaitingForUser;
    currentReport.userPrompt = std::move(prompt);
    currentReport.userStepDeadline = timeMs + kConnectionCheckUserStepTimeoutMs;
    changed();
}

void ConnectionCheck::skipUserStep(TimeMs timeMs)
{
    if (currentReport.state != ConnectionCheckState::WaitingForUser)
        return;

    const auto skipped = step;
    currentReport.state = ConnectionCheckState::Running;
    currentReport.userPrompt.clear();
    if (skipped == Step::WaitingForAnyMidi) {
        set(ConnectionCheckItem::MidiFromSynth, ConnectionCheckOutcome::NotTested,
            "Not tested: the step was skipped.", "Run the check again and play a key when asked.");
        set(ConnectionCheckItem::SysExAnswered, ConnectionCheckOutcome::Failed,
            "The synth did not answer the editor's SysEx requests on any of the 16 Global Channels.",
            kOutCableFix + " " + kReturnCableFix);
        setRemaining(ConnectionCheckOutcome::NotTested, "Not tested: the synth does not answer SysEx requests.");
        finish();
    } else if (skipped == Step::WaitingForKnob) {
        set(ConnectionCheckItem::CcsTransmitted, ConnectionCheckOutcome::NotTested,
            "Not tested: the synth did not answer the All Controllers Request, and the step was skipped.",
            "Run the check again and turn a knob when asked.");
        startCcReceive(timeMs);
    }
}

void ConnectionCheck::handleIncomingMidi(std::span<const std::uint8_t> message, TimeMs timeMs)
{
    switch (step) {
    case Step::FindingSynth:
        midiSeen = midiSeen || isFromSynth(message);
        break;
    case Step::WaitingForAnyMidi:
        if (isFromSynth(message)) {
            currentReport.state = ConnectionCheckState::Running;
            currentReport.userPrompt.clear();
            midiButNoSysEx();
        }
        break;
    case Step::AllControllers:
        ccSeen = ccSeen || isPanelCc(message);
        break;
    case Step::WaitingForKnob:
        if (isPanelCc(message)) {
            currentReport.state = ConnectionCheckState::Running;
            currentReport.userPrompt.clear();
            ccsTransmitted("The synth sends CCs: a CC arrived when you turned a knob (it did not answer the All "
                           "Controllers Request).",
                           timeMs);
        }
        break;
    case Step::None:
    case Step::ReadingSlot:
    case Step::SettlingCc:
    case Step::ReadingBack:
        break;
    }
}

void ConnectionCheck::advanceTime(TimeMs timeMs)
{
    if (currentReport.state == ConnectionCheckState::WaitingForUser && timeMs >= currentReport.userStepDeadline) {
        currentReport.state = ConnectionCheckState::Running;
        currentReport.userPrompt.clear();
        if (step == Step::WaitingForAnyMidi)
            noMidiAtAll();
        else if (step == Step::WaitingForKnob)
            ccsNotTransmitted(timeMs);
        return;
    }

    if (step == Step::SettlingCc && timeMs >= settleUntil)
        readBack(timeMs);
}

void ConnectionCheck::finish()
{
    step = Step::None;
    currentReport.state = ConnectionCheckState::Finished;
    currentReport.userPrompt.clear();
    changed();
}

void ConnectionCheck::set(ConnectionCheckItem item, ConnectionCheckOutcome outcome, std::string explanation,
                          std::string fix)
{
    auto& result = currentReport.results[static_cast<std::size_t>(item)];
    result.outcome = outcome;
    result.explanation = std::move(explanation);
    result.fix = std::move(fix);
    changed();
}

void ConnectionCheck::setRemaining(ConnectionCheckOutcome outcome, const std::string& explanation)
{
    for (auto& result : currentReport.results)
        if (result.outcome == ConnectionCheckOutcome::Pending || result.outcome == ConnectionCheckOutcome::Running) {
            result.outcome = outcome;
            result.explanation = explanation;
            result.fix.clear();
        }
    changed();
}

} // namespace nl2x
