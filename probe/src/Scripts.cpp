// The probe scripts of docs/hardware-discovery-plan.md, each a small state machine.

#include "Script.h"

#include "nl2x/HardwareAssumptions.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <utility>

namespace nl2x::probe {

namespace {

using Values = std::vector<std::int8_t>;

constexpr std::uint8_t kControlChange = 0xB0;

// Decoded Program offsets [OFF-2X p.111].
const std::size_t kCutoffOffset = programParameterInfo(ProgramParameter::FilterCutoff).sysExOffset;
constexpr std::size_t kCutoffMorphAmountOffset = 28; // cutoff_sens

// Program 99 of a Bank: the location the destructive Bank test overwrites.
constexpr std::uint8_t kTestLocation = 98;
constexpr int kProgramsPerBank = 99;
// Reading is harmless, but the synth still has to answer each request: keep a small gap.
constexpr TimeMs kBankReadSpacingMs = 50;

std::string shownChannel(MidiChannel channel)
{
    return std::to_string(channel + 1);
}

// "Global Channel 16 (dev 15)", with the display assumption of HardwareAssumptions.h.
std::string describeGlobalChannel(GlobalChannel channel)
{
    return "Global Channel " + std::to_string(channel + hardware::kShownGlobalChannelOfDev0) + " (dev "
           + std::to_string(channel) + ")";
}

std::string joined(const std::vector<std::string>& parts, std::string_view separator)
{
    std::string text;
    for (const auto& part : parts) {
        if (! text.empty())
            text += separator;
        text += part;
    }
    return text;
}

bool isControlChange(std::span<const std::uint8_t> message)
{
    return message.size() == 3 && (message[0] & 0xF0) == kControlChange;
}

// ---------------------------------------------------------------------------------------------
// A list of requests, each given a fixed listening window; whatever comes back in a window is
// credited to that request.

struct SweepItem {
    std::string label;
    std::vector<std::uint8_t> message;
    // Set when the item probes one Global Channel.
    std::optional<GlobalChannel> probedChannel;
};

class RequestSweep final : public ProbeSession::Script {
public:
    RequestSweep(ProbeSession::State& state, std::string_view scriptName, std::vector<SweepItem> sweepItems,
                 TimeMs listenMs, bool adoptGlobalChannelWhenUnique)
        : Script(state, scriptName),
          items(std::move(sweepItems)),
          answers(items.size()),
          windowMs(listenMs),
          adoptGlobalChannel(adoptGlobalChannelWhenUnique)
    {
    }

    void start(TimeMs nowMs) override { sendCurrent(nowMs); }

    void handleIncomingMidi(std::span<const std::uint8_t> message, const ParsedSysEx* sysEx, TimeMs) override
    {
        auto& answer = answers[index];
        if (sysEx != nullptr) {
            answer.sysEx.push_back(describeSysEx(*sysEx));
        } else if (isControlChange(message)) {
            ++answer.ccCount;
            answer.ccChannels.set(message[0] & 0x0F);
        }
    }

    void advanceTime(TimeMs nowMs) override
    {
        if (nowMs < windowEnd)
            return;
        if (index + 1 == items.size()) {
            adoptChannel();
            complete();
            return;
        }
        ++index;
        sendCurrent(nowMs);
    }

protected:
    std::string summary() const override
    {
        std::vector<std::string> hits;
        for (std::size_t i = 0; i <= index; ++i) {
            if (const auto text = describe(answers[i]); ! text.empty())
                hits.push_back(items[i].label + " -> " + text);
        }
        auto text = std::to_string(hits.size()) + " of " + std::to_string(index + 1) + " requests answered";
        if (! hits.empty())
            text += ": " + joined(hits, " | ");
        if (! adoption.empty())
            text += ". " + adoption;
        return text;
    }

private:
    struct Answer {
        // Described at once: a sweep may collect many large replies.
        std::vector<std::string> sysEx;
        int ccCount = 0;
        std::bitset<16> ccChannels;
    };

    static std::string describe(const Answer& answer)
    {
        auto parts = answer.sysEx;
        if (answer.ccCount > 0) {
            std::vector<std::string> channels;
            for (MidiChannel channel = 0; channel < 16; ++channel)
                if (answer.ccChannels.test(channel))
                    channels.push_back(shownChannel(channel));
            parts.push_back(std::to_string(answer.ccCount) + " CCs on channel " + joined(channels, ", "));
        }
        return joined(parts, "; ");
    }

    void sendCurrent(TimeMs nowMs)
    {
        windowEnd = nowMs + windowMs;
        send(items[index].message);
    }

    void adoptChannel()
    {
        if (! adoptGlobalChannel)
            return;

        std::set<GlobalChannel> answered;
        for (std::size_t i = 0; i < items.size(); ++i)
            if (items[i].probedChannel && ! answers[i].sysEx.empty())
                answered.insert(*items[i].probedChannel);

        if (answered.size() == 1) {
            session.globalChannel = *answered.begin();
            adoption = "Adopted " + describeGlobalChannel(session.globalChannel);
        } else if (answered.empty()) {
            adoption = "No dev answered: Global Channel unchanged";
        } else {
            adoption = std::to_string(answered.size()) + " devs answered: Global Channel unchanged";
        }
    }

    std::vector<SweepItem> items;
    std::vector<Answer> answers;
    std::size_t index = 0;
    TimeMs windowMs;
    TimeMs windowEnd = 0;
    bool adoptGlobalChannel;
    std::string adoption;
};

std::unique_ptr<ProbeSession::Script> makeSweep(ProbeSession::State& state, ScriptId id,
                                                std::vector<SweepItem> items, TimeMs listenMs,
                                                bool adoptGlobalChannel = false)
{
    return std::make_unique<RequestSweep>(state, scriptInfo(id).name, std::move(items), listenMs, adoptGlobalChannel);
}

std::string requestLabel(std::uint8_t type, std::uint8_t spec)
{
    return "type " + std::to_string(type) + " spec " + std::to_string(spec);
}

std::vector<SweepItem> singleRequest(GlobalChannel channel, std::uint8_t type, std::uint8_t spec)
{
    return { { requestLabel(type, spec), buildRequest(channel, type, spec), std::nullopt } };
}

std::vector<SweepItem> slotRequests(GlobalChannel channel, std::uint8_t type)
{
    std::vector<SweepItem> items;
    for (auto slot : kAllSlots)
        items.push_back({ "Slot " + std::string(1, slotLetter(slot)) + " (" + requestLabel(type, static_cast<std::uint8_t>(slotIndex(slot))) + ")",
                          buildRequest(channel, type, static_cast<std::uint8_t>(slotIndex(slot))), std::nullopt });
    return items;
}

std::vector<SweepItem> everyDev(std::uint8_t type, std::uint8_t spec)
{
    std::vector<SweepItem> items;
    for (GlobalChannel dev = 0; dev < 16; ++dev)
        items.push_back({ "dev " + std::to_string(dev), buildRequest(dev, type, spec), dev });
    return items;
}

std::vector<SweepItem> bankScan(GlobalChannel channel)
{
    static constexpr std::array<std::uint8_t, 5> kSpecs { 0, 1, 98, 99, 108 };
    std::vector<SweepItem> items;
    for (auto type = hardware::kFirstBankRequestTypeCandidate; type <= hardware::kLastBankRequestTypeCandidate; ++type)
        for (auto spec : kSpecs)
            items.push_back({ requestLabel(type, spec), buildRequest(channel, type, spec), std::nullopt });
    return items;
}

// ---------------------------------------------------------------------------------------------
// Base for scripts that start by reading the Slot A Program.

class SlotAScript : public ProbeSession::Script {
public:
    using Script::Script;

    void start(TimeMs nowMs) override
    {
        reading = true;
        sendRequest(slotEditBufferRequest(globalChannel(), Slot::A), SysExKind::ProgramDump, nowMs);
    }

    void handleIncomingMidi(std::span<const std::uint8_t> message, const ParsedSysEx* sysEx, TimeMs nowMs) override
    {
        if (reading) {
            if (const auto reply = acceptReply(sysEx)) {
                reading = false;
                slotA = *reply->values;
                programRead(nowMs);
            }
            return;
        }
        handleAfterRead(message, sysEx, nowMs);
    }

    void advanceTime(TimeMs nowMs) override
    {
        if (reading) {
            if (replyTimedOut(nowMs))
                fail("no reply to the Slot A request (type 14, spec 0); check the Global Channel and that Slot A "
                     "plays a Program");
            return;
        }
        advanceAfterRead(nowMs);
    }

protected:
    virtual void programRead(TimeMs nowMs) = 0;
    virtual void handleAfterRead(std::span<const std::uint8_t>, const ParsedSysEx*, TimeMs) {}
    virtual void advanceAfterRead(TimeMs nowMs) = 0;

    // Sends a Program to Slot A's Edit Buffer and remembers it as the probe's own change.
    void sendToSlotA(const Values& program)
    {
        session.slotALastSent = program;
        send(programEditBufferDump(globalChannel(), Slot::A, program));
    }

    // The Slot A Program as it was before the probe changed it: if Slot A still holds the probe's
    // last change, the original read earlier is kept.
    const Values& captureOriginal()
    {
        if (! session.slotAOriginal || ! session.slotALastSent || *session.slotALastSent != slotA)
            session.slotAOriginal = slotA;
        return *session.slotAOriginal;
    }

    Values slotA;

private:
    bool reading = false;
};

// Sends edited copies of the original Slot A Program, holding each for a while.
struct ProgramEdit {
    std::string label;
    std::function<void(Values&)> apply;
    TimeMs holdMs;
};

class SendProgramToSlotA final : public SlotAScript {
public:
    SendProgramToSlotA(ProbeSession::State& state, std::string_view scriptName, std::vector<ProgramEdit> programEdits)
        : SlotAScript(state, scriptName), edits(std::move(programEdits))
    {
    }

protected:
    void programRead(TimeMs nowMs) override
    {
        original = captureOriginal();
        sendEdit(nowMs);
    }

    void advanceAfterRead(TimeMs nowMs) override
    {
        if (nowMs < nextEditAt)
            return;
        if (++editIndex == edits.size()) {
            complete();
            return;
        }
        sendEdit(nowMs);
    }

    std::string summary() const override
    {
        if (original.empty())
            return "nothing sent: Slot A was not read yet";
        std::vector<std::string> labels;
        for (std::size_t i = 0; i < sent; ++i)
            labels.push_back(edits[i].label);
        return "sent Slot A Program with " + joined(labels, ", then ") + " (original Filter Cutoff "
               + std::to_string(original[kCutoffOffset]) + ", Morph Amount on Filter Cutoff "
               + std::to_string(original[kCutoffMorphAmountOffset])
               + "). 'Restore Slot A Program' sends the original back";
    }

private:
    void sendEdit(TimeMs nowMs)
    {
        auto program = original;
        edits[editIndex].apply(program);
        report("Sending Slot A Program with " + edits[editIndex].label);
        sendToSlotA(program);
        ++sent;
        nextEditAt = nowMs + edits[editIndex].holdMs;
    }

    std::vector<ProgramEdit> edits;
    Values original;
    std::size_t editIndex = 0;
    std::size_t sent = 0;
    TimeMs nextEditAt = 0;
};

class RestoreSlotAProgram final : public ProbeSession::Script {
public:
    using Script::Script;

    std::string preconditionFailure() const override
    {
        return session.slotAOriginal ? std::string() : "no Slot A Program was changed by the probe in this session";
    }

    void start(TimeMs) override
    {
        session.slotALastSent = *session.slotAOriginal;
        send(programEditBufferDump(globalChannel(), Slot::A, *session.slotAOriginal));
        complete();
    }
    void handleIncomingMidi(std::span<const std::uint8_t>, const ParsedSysEx*, TimeMs) override {}
    void advanceTime(TimeMs) override {}

protected:
    std::string summary() const override { return "sent the original Slot A Program back to Slot A's Edit Buffer"; }
};

// ---------------------------------------------------------------------------------------------

class TogglePerformanceSplit final : public ProbeSession::Script {
public:
    using Script::Script;

    void start(TimeMs nowMs) override
    {
        sendRequest(performanceEditBufferRequest(globalChannel()), SysExKind::PerformanceDump, nowMs);
    }

    void handleIncomingMidi(std::span<const std::uint8_t>, const ParsedSysEx* sysEx, TimeMs) override
    {
        const auto reply = acceptReply(sysEx);
        if (! reply)
            return;
        auto performance = *reply->values;
        before = performance[kPerformanceKeyboardSplitOffset];
        after = static_cast<std::int8_t>(before == 0 ? 1 : 0);
        performance[kPerformanceKeyboardSplitOffset] = after;
        send(performanceEditBufferDump(globalChannel(), performance));
        complete();
    }

    void advanceTime(TimeMs nowMs) override
    {
        if (replyTimedOut(nowMs))
            fail("no reply to the Performance Edit Buffer request (type 40, spec 0)");
    }

protected:
    std::string summary() const override
    {
        return "Keyboard Split was " + std::to_string(before) + "; sent the Performance Edit Buffer (type 30) with "
               "Keyboard Split " + std::to_string(after) + ". Run again to toggle it back";
    }

private:
    std::int8_t before = 0;
    std::int8_t after = 0;
};

// ---------------------------------------------------------------------------------------------

class WatchCcs final : public ProbeSession::Script {
public:
    using Script::Script;

    void start(TimeMs) override { report("Watching CCs: operate the synth, then press Stop"); }

    void handleIncomingMidi(std::span<const std::uint8_t> message, const ParsedSysEx*, TimeMs) override
    {
        if (isControlChange(message))
            seen[{ static_cast<MidiChannel>(message[0] & 0x0F), message[1] }].set(message[2]);
    }

    void advanceTime(TimeMs) override {}

protected:
    bool endsWhenStopped() const override { return true; }

    void reportDetails() override
    {
        for (const auto& [key, values] : seen)
            report("CC range: " + describe(key.first, key.second, values));
    }

    std::string summary() const override
    {
        std::vector<std::string> ccs;
        for (const auto& [key, values] : seen)
            ccs.push_back("CC" + std::to_string(key.second) + " ch " + shownChannel(key.first));
        return std::to_string(seen.size()) + " controllers seen" + (ccs.empty() ? "" : ": " + joined(ccs, ", "));
    }

private:
    // "CC35 on channel 1: 4 values: 0 1 2 3", or for many values
    // "CC78 on channel 1: 61 values from 0 to 120, steps 2".
    static std::string describe(MidiChannel channel, std::uint8_t cc, const std::bitset<128>& values)
    {
        std::vector<int> list;
        for (int value = 0; value < 128; ++value)
            if (values.test(static_cast<std::size_t>(value)))
                list.push_back(value);

        auto text = "CC" + std::to_string(cc) + " on channel " + shownChannel(channel) + ": "
                    + std::to_string(list.size()) + (list.size() == 1 ? " value" : " values");
        if (list.size() <= 16) {
            text += ":";
            for (auto value : list)
                text += " " + std::to_string(value);
            return text;
        }

        std::set<int> steps;
        for (std::size_t i = 1; i < list.size(); ++i)
            steps.insert(list[i] - list[i - 1]);
        std::vector<std::string> stepTexts;
        for (auto step : steps)
            stepTexts.push_back(std::to_string(step));
        return text + " from " + std::to_string(list.front()) + " to " + std::to_string(list.back()) + ", steps "
               + joined(stepTexts, " ");
    }

    std::map<std::pair<MidiChannel, std::uint8_t>, std::bitset<128>> seen;
};

class CcSweep final : public ProbeSession::Script {
public:
    static constexpr TimeMs kSpacingMs = 250;

    CcSweep(ProbeSession::State& state, std::string_view scriptName, std::uint8_t sweptCc, MidiChannel sweptChannel)
        : Script(state, scriptName), cc(sweptCc), channel(sweptChannel)
    {
    }

    std::string preconditionFailure() const override
    {
        return cc < 128 && channel < 16 ? std::string() : "the CC must be 0–127 and the channel 1–16";
    }

    void start(TimeMs nowMs) override { sendValue(nowMs); }
    void handleIncomingMidi(std::span<const std::uint8_t>, const ParsedSysEx*, TimeMs) override {}

    void advanceTime(TimeMs nowMs) override
    {
        if (nowMs < nextAt)
            return;
        if (value == 127) {
            complete();
            return;
        }
        ++value;
        sendValue(nowMs);
    }

protected:
    std::string summary() const override
    {
        return "sent CC" + std::to_string(cc) + " values 0–" + std::to_string(value) + " on channel "
               + shownChannel(channel) + ", " + std::to_string(kSpacingMs) + " ms apart";
    }

private:
    void sendValue(TimeMs nowMs)
    {
        const std::array<std::uint8_t, 3> message { static_cast<std::uint8_t>(kControlChange | channel), cc, value };
        send(message);
        nextAt = nowMs + kSpacingMs;
    }

    std::uint8_t cc;
    MidiChannel channel;
    std::uint8_t value = 0;
    TimeMs nextAt = 0;
};

// ---------------------------------------------------------------------------------------------
// 20 Program dumps per spacing, each read back; the spacing is the gap between handing one message
// to the MIDI port and the next (dump → request, reply → next dump).

class PacingTest final : public SlotAScript {
public:
    static constexpr std::array<TimeMs, 5> kSpacingsMs { 0, 5, 10, 20, 50 };
    static constexpr int kRounds = 20;

    using SlotAScript::SlotAScript;

protected:
    void programRead(TimeMs nowMs) override
    {
        original = slotA;
        beginSpacing();
        sendDump(nowMs);
    }

    void handleAfterRead(std::span<const std::uint8_t>, const ParsedSysEx* sysEx, TimeMs nowMs) override
    {
        if (phase != Phase::ReadingBack)
            return;
        if (const auto reply = acceptReply(sysEx)) {
            if (*reply->values == sent)
                ++results.back().matches;
            endRound(nowMs);
        }
    }

    void advanceAfterRead(TimeMs nowMs) override
    {
        switch (phase) {
        case Phase::BeforeRequest:
            if (nowMs >= nextAt)
                sendReadBack(nowMs);
            break;
        case Phase::ReadingBack:
            if (replyTimedOut(nowMs))
                endRound(nowMs);
            break;
        case Phase::BeforeDump:
            if (nowMs >= nextAt)
                sendDump(nowMs);
            break;
        }
    }

    std::string summary() const override
    {
        std::vector<std::string> parts;
        std::optional<TimeMs> smallestPass;
        for (std::size_t i = 0; i < results.size(); ++i) {
            const auto pass = results[i].matches == kRounds && results[i].rounds == kRounds;
            parts.push_back(std::to_string(kSpacingsMs[i]) + " ms " + (pass ? "PASS" : "FAIL"));
            if (pass && ! smallestPass)
                smallestPass = kSpacingsMs[i];
        }
        return joined(parts, ", ") + "; smallest spacing with every read-back matching: "
               + (smallestPass ? std::to_string(*smallestPass) + " ms" : std::string("none"))
               + (restored ? "; Slot A Program restored" : "");
    }

private:
    enum class Phase { BeforeRequest, ReadingBack, BeforeDump };

    struct Result {
        int rounds = 0;
        int matches = 0;
        TimeMs minMeasured = 0;
        TimeMs maxMeasured = 0;
    };

    TimeMs spacing() const { return kSpacingsMs[spacingIndex]; }

    void beginSpacing() { results.push_back({}); }

    void sendDump(TimeMs nowMs)
    {
        sent = original;
        // A marker that differs from the previous round's, so a lost dump shows as a mismatch.
        sent[kCutoffOffset] = static_cast<std::int8_t>((round * 5 + static_cast<int>(spacingIndex) * 3 + 1) & 0x7F);
        dumpSentAt = nowMs;
        send(programEditBufferDump(globalChannel(), Slot::A, sent));
        phase = Phase::BeforeRequest;
        nextAt = nowMs + spacing();
        if (spacing() == 0)
            sendReadBack(nowMs);
    }

    void sendReadBack(TimeMs nowMs)
    {
        auto& result = results.back();
        const auto measured = nowMs - dumpSentAt;
        result.minMeasured = result.rounds == 0 ? measured : std::min(result.minMeasured, measured);
        result.maxMeasured = result.rounds == 0 ? measured : std::max(result.maxMeasured, measured);
        ++result.rounds;
        phase = Phase::ReadingBack;
        sendRequest(slotEditBufferRequest(globalChannel(), Slot::A), SysExKind::ProgramDump, nowMs);
    }

    void endRound(TimeMs nowMs)
    {
        if (++round == kRounds) {
            const auto& result = results.back();
            report("Pacing " + std::to_string(spacing()) + " ms (measured " + std::to_string(result.minMeasured) + "–"
                   + std::to_string(result.maxMeasured) + " ms): " + std::to_string(result.matches) + " of "
                   + std::to_string(kRounds) + " read-backs match: " + (result.matches == kRounds ? "PASS" : "FAIL"));
            round = 0;
            if (++spacingIndex == kSpacingsMs.size()) {
                send(programEditBufferDump(globalChannel(), Slot::A, original));
                restored = true;
                complete();
                return;
            }
            beginSpacing();
        }
        phase = Phase::BeforeDump;
        nextAt = nowMs + spacing();
        if (spacing() == 0)
            sendDump(nowMs);
    }

    Values original;
    Values sent;
    std::vector<Result> results;
    std::size_t spacingIndex = 0;
    int round = 0;
    Phase phase = Phase::BeforeDump;
    TimeMs nextAt = 0;
    TimeMs dumpSentAt = 0;
    bool restored = false;
};

// ---------------------------------------------------------------------------------------------

class StressProgramDumps final : public SlotAScript {
public:
    static constexpr TimeMs kSpacingMs = 50;
    static constexpr int kDumps = 600; // 30 s

    using SlotAScript::SlotAScript;

protected:
    void programRead(TimeMs nowMs) override
    {
        original = slotA;
        sendDump(nowMs);
    }

    void handleAfterRead(std::span<const std::uint8_t>, const ParsedSysEx* sysEx, TimeMs) override
    {
        if (verifying) {
            if (const auto reply = acceptReply(sysEx)) {
                unchanged = *reply->values == original;
                complete();
            }
            return;
        }
        ++incoming;
        if (sysEx != nullptr) {
            ++incomingSysEx;
            if (sysEx->kind == SysExKind::NotNordLead || ! sysEx->values)
                ++malformedSysEx;
        }
    }

    void advanceAfterRead(TimeMs nowMs) override
    {
        if (verifying) {
            if (replyTimedOut(nowMs))
                complete();
            return;
        }
        if (nowMs < nextAt)
            return;
        if (sent == kDumps) {
            verifying = true;
            sendRequest(slotEditBufferRequest(globalChannel(), Slot::A), SysExKind::ProgramDump, nowMs);
            return;
        }
        sendDump(nowMs);
    }

    std::string summary() const override
    {
        const std::string verdict = unchanged ? (*unchanged ? "unchanged" : "CHANGED")
                                              : (verifying ? "no reply to the final read-back" : "not read back");
        return "sent " + std::to_string(sent) + " Program dumps " + std::to_string(kSpacingMs)
               + " ms apart; received " + std::to_string(incoming) + " messages meanwhile, "
               + std::to_string(incomingSysEx) + " SysEx, " + std::to_string(malformedSysEx)
               + " malformed SysEx; Slot A Program read back afterwards: " + verdict;
    }

private:
    void sendDump(TimeMs nowMs)
    {
        send(programEditBufferDump(globalChannel(), Slot::A, original));
        ++sent;
        nextAt = nowMs + kSpacingMs;
    }

    Values original;
    int sent = 0;
    int incoming = 0;
    int incomingSysEx = 0;
    int malformedSysEx = 0;
    TimeMs nextAt = 0;
    bool verifying = false;
    std::optional<bool> unchanged;
};

// ---------------------------------------------------------------------------------------------
// Bank backup, test write and restore. Only the write and the restore store anything.

std::string bankRequestMissing(const ScriptOptions& options)
{
    return options.bankRequestType ? std::string()
                                   : "set the Bank request type found by 'Scan Bank requests' (plan step 5)";
}

class BackUpBank final : public ProbeSession::Script {
public:
    BackUpBank(ProbeSession::State& state, std::string_view scriptName, const ScriptOptions& scriptOptions)
        : Script(state, scriptName), options(scriptOptions)
    {
    }

    std::string preconditionFailure() const override { return bankRequestMissing(options); }

    void start(TimeMs nowMs) override
    {
        backup.requestType = *options.bankRequestType;
        requestNext(nowMs);
    }

    void handleIncomingMidi(std::span<const std::uint8_t> message, const ParsedSysEx* sysEx, TimeMs nowMs) override
    {
        const auto reply = acceptReply(sysEx);
        if (! reply)
            return;
        if (static_cast<std::size_t>(reply->spec) == backup.replies.size())
            ++specMatches;
        replyTypes.insert(reply->type);
        backup.replies.emplace_back(message.begin(), message.end());
        if (backup.replies.size() == kProgramsPerBank) {
            session.bankBackup = backup;
            if (session.onBankBackupCaptured)
                session.onBankBackupCaptured(*session.bankBackup);
            complete();
            return;
        }
        nextAt = nowMs + kBankReadSpacingMs;
    }

    void advanceTime(TimeMs nowMs) override
    {
        if (replyTimedOut(nowMs)) {
            fail("Program " + std::to_string(backup.replies.size() + 1) + " did not answer (request type "
                 + std::to_string(backup.requestType) + "); no backup kept");
            return;
        }
        if (! awaitingReply() && nowMs >= nextAt)
            requestNext(nowMs);
    }

protected:
    std::string summary() const override
    {
        std::vector<std::string> types;
        for (auto type : replyTypes)
            types.push_back(std::to_string(type));
        const auto complete = backup.replies.size() == kProgramsPerBank;
        return (complete ? "captured all 99 Programs" : "captured " + std::to_string(backup.replies.size())
                                                            + " of 99 Programs, no backup kept")
               + " with request type " + std::to_string(backup.requestType) + "; reply type bytes: "
               + joined(types, " ") + "; reply spec equals the location for " + std::to_string(specMatches) + " of "
               + std::to_string(backup.replies.size());
    }

private:
    void requestNext(TimeMs nowMs)
    {
        sendRequest(bankLocationRequest(globalChannel(), backup.requestType,
                                        static_cast<std::uint8_t>(backup.replies.size())),
                    SysExKind::ProgramDump, nowMs);
    }

    ScriptOptions options;
    BankBackup backup;
    std::set<std::uint8_t> replyTypes;
    std::size_t specMatches = 0;
    TimeMs nextAt = 0;
};

// Why a write to the Bank must not start; empty when it may.
std::string bankWriteRefusal(const ProbeSession::State& state, const ScriptOptions& options)
{
    if (const auto missing = bankRequestMissing(options); ! missing.empty())
        return missing;
    if (! state.bankBackup || state.bankBackup->requestType != *options.bankRequestType)
        return "no backup of the Bank read with request type " + std::to_string(*options.bankRequestType)
               + " was captured in this session: run 'Back up Bank' first";
    if (options.bankDumpType < hardware::kFirstProgramBankDumpType
        || options.bankDumpType > hardware::kLastProgramBankDumpType)
        return "the Bank dump type must be 1–10";
    for (const auto& reply : state.bankBackup->replies) {
        const auto parsed = parseSysEx(reply);
        if (parsed.kind != SysExKind::ProgramDump || ! parsed.values)
            return "the backup holds a reply that is not a Program dump";
    }
    return {};
}

Values backedUpProgram(const BankBackup& backup, std::size_t location)
{
    return *parseSysEx(backup.replies[location]).values;
}

class WriteTestProgramToBank final : public ProbeSession::Script {
public:
    static constexpr TimeMs kStoreWaitMs = 500;

    WriteTestProgramToBank(ProbeSession::State& state, std::string_view scriptName, const ScriptOptions& scriptOptions)
        : Script(state, scriptName), options(scriptOptions)
    {
    }

    std::string preconditionFailure() const override { return bankWriteRefusal(session, options); }

    void start(TimeMs nowMs) override
    {
        report("WRITES TO THE SYNTH'S MEMORY: storing a test Program at Program 99 of the Bank selected on the synth");
        previous = backedUpProgram(*session.bankBackup, kTestLocation);
        test = previous;
        test[kCutoffOffset] = static_cast<std::int8_t>(previous[kCutoffOffset] < 64 ? 127 : 0);
        send(bankProgramDump(globalChannel(), options.bankDumpType, kTestLocation, test));
        readBackAt = nowMs + kStoreWaitMs;
    }

    void handleIncomingMidi(std::span<const std::uint8_t>, const ParsedSysEx* sysEx, TimeMs) override
    {
        if (const auto reply = acceptReply(sysEx)) {
            const auto& values = *reply->values;
            verdict = values == test       ? "the test Program was stored at Program 99"
                      : values == previous ? "Program 99 is unchanged: the test Program was not stored there (check "
                                             "which Bank is selected on the synth)"
                                           : "Program 99 differs from both the test Program and the backup";
            complete();
        }
    }

    void advanceTime(TimeMs nowMs) override
    {
        if (! readingBack && nowMs >= readBackAt) {
            readingBack = true;
            sendRequest(bankLocationRequest(globalChannel(), *options.bankRequestType, kTestLocation),
                        SysExKind::ProgramDump, nowMs);
        } else if (replyTimedOut(nowMs)) {
            verdict = "no reply to the read-back of Program 99";
            complete();
        }
    }

protected:
    std::string summary() const override
    {
        return "sent a test Program (dump type " + std::to_string(options.bankDumpType)
               + ", Program 99, Filter Cutoff " + std::to_string(test[kCutoffOffset]) + "); " + verdict
               + ". Run 'Restore Bank backup' to put the Bank back";
    }

private:
    ScriptOptions options;
    Values previous;
    Values test;
    TimeMs readBackAt = 0;
    bool readingBack = false;
    std::string verdict = "not read back";
};

class RestoreBankBackup final : public ProbeSession::Script {
public:
    RestoreBankBackup(ProbeSession::State& state, std::string_view scriptName, const ScriptOptions& scriptOptions)
        : Script(state, scriptName), options(scriptOptions)
    {
    }

    std::string preconditionFailure() const override { return bankWriteRefusal(session, options); }

    void start(TimeMs nowMs) override
    {
        report("WRITES TO THE SYNTH'S MEMORY: sending the 99 backed-up Programs into the Bank selected on the synth");
        writeNext(nowMs);
    }

    void handleIncomingMidi(std::span<const std::uint8_t>, const ParsedSysEx* sysEx, TimeMs nowMs) override
    {
        if (const auto reply = acceptReply(sysEx)) {
            if (*reply->values == backedUpProgram(*session.bankBackup, location))
                ++matches;
            nextVerify(nowMs);
        }
    }

    void advanceTime(TimeMs nowMs) override
    {
        if (! verifying) {
            if (nowMs >= nextAt)
                writeNext(nowMs);
            return;
        }
        if (replyTimedOut(nowMs))
            nextVerify(nowMs);
        else if (! awaitingReply() && nowMs >= nextAt)
            requestCurrent(nowMs);
    }

protected:
    std::string summary() const override
    {
        return "wrote " + std::to_string(written) + " Programs " + std::to_string(hardware::kBankWriteSpacingMs)
               + " ms apart (dump type " + std::to_string(options.bankDumpType) + "); read-back: "
               + std::to_string(matches) + " of " + std::to_string(verified) + " match the backup";
    }

private:
    void writeNext(TimeMs nowMs)
    {
        if (written == kProgramsPerBank) {
            verifying = true;
            location = 0;
            nextAt = nowMs + hardware::kBankWriteSpacingMs;
            return;
        }
        send(bankProgramDump(globalChannel(), options.bankDumpType, static_cast<std::uint8_t>(written),
                             backedUpProgram(*session.bankBackup, static_cast<std::size_t>(written))));
        ++written;
        nextAt = nowMs + hardware::kBankWriteSpacingMs;
    }

    void requestCurrent(TimeMs nowMs)
    {
        ++verified;
        sendRequest(bankLocationRequest(globalChannel(), *options.bankRequestType, static_cast<std::uint8_t>(location)),
                    SysExKind::ProgramDump, nowMs);
    }

    void nextVerify(TimeMs nowMs)
    {
        if (++location == kProgramsPerBank) {
            complete();
            return;
        }
        nextAt = nowMs + kBankReadSpacingMs;
    }

    ScriptOptions options;
    int written = 0;
    int verified = 0;
    int matches = 0;
    std::size_t location = 0;
    TimeMs nextAt = 0;
    bool verifying = false;
};

} // namespace

std::unique_ptr<ProbeSession::Script> makeScript(ScriptId id, ProbeSession::State& state, const ScriptOptions& options)
{
    using namespace hardware;
    const auto channel = state.globalChannel;
    const auto name = scriptInfo(id).name;

    switch (id) {
    case ScriptId::AllControllersRequest:
        return makeSweep(state, id, everyDev(kAllControllersRequestType, 0), 300);
    case ScriptId::ProbeGlobalChannel:
        return makeSweep(state, id, everyDev(kSlotEditBufferRequestType, 0), 300, true);
    case ScriptId::RequestSlotsType14:
        return makeSweep(state, id, slotRequests(channel, kSlotEditBufferRequestType), 1000);
    case ScriptId::RequestSlotsType10:
        return makeSweep(state, id, slotRequests(channel, kDocumentedEditBufferRequestType), 1000);
    case ScriptId::RequestKitType10Spec16:
        return makeSweep(state, id,
                         singleRequest(channel, kDocumentedEditBufferRequestType, kPercussionKitEditBufferSpecOfSlotA),
                         1500);
    case ScriptId::RequestKitType14Spec16:
        return makeSweep(state, id,
                         singleRequest(channel, kSlotEditBufferRequestType, kPercussionKitEditBufferSpecOfSlotA), 1500);
    case ScriptId::RequestKitType14Spec0:
        return makeSweep(state, id, singleRequest(channel, kSlotEditBufferRequestType, 0), 1500);
    case ScriptId::ScanBankRequests:
        return makeSweep(state, id, bankScan(channel), 400);
    case ScriptId::RequestPerformanceEditBuffer:
        return makeSweep(state, id, singleRequest(channel, kPerformanceEditBufferRequestType, 0), 1500);
    case ScriptId::RequestPerformanceBank1Location0:
        return makeSweep(state, id, singleRequest(channel, kFirstPerformanceBankRequestType, 0), 1500);
    case ScriptId::SendProgramCutoffLowHigh:
        return std::make_unique<SendProgramToSlotA>(
            state, name,
            std::vector<ProgramEdit> {
                { "Filter Cutoff 0", [](Values& p) { p[kCutoffOffset] = 0; }, 3000 },
                { "Filter Cutoff 127", [](Values& p) { p[kCutoffOffset] = 127; }, 0 },
            });
    case ScriptId::SendProgramMorphOnCutoff:
        return std::make_unique<SendProgramToSlotA>(
            state, name,
            std::vector<ProgramEdit> {
                { "Filter Cutoff 20 and Morph Amount +100 on Filter Cutoff",
                  [](Values& p) {
                      p[kCutoffOffset] = 20;
                      p[kCutoffMorphAmountOffset] = 100;
                  },
                  0 },
            });
    case ScriptId::RestoreSlotAProgram:
        return std::make_unique<RestoreSlotAProgram>(state, name);
    case ScriptId::SendPerformanceToggleSplit:
        return std::make_unique<TogglePerformanceSplit>(state, name);
    case ScriptId::WatchCcs:
        return std::make_unique<WatchCcs>(state, name);
    case ScriptId::SendCcSweep:
        return std::make_unique<CcSweep>(state, name, options.sweepCc, options.sweepChannel);
    case ScriptId::PacingTest:
        return std::make_unique<PacingTest>(state, name);
    case ScriptId::StressProgramDumps:
        return std::make_unique<StressProgramDumps>(state, name);
    case ScriptId::BackUpBank:
        return std::make_unique<BackUpBank>(state, name, options);
    case ScriptId::WriteTestProgramToBank:
        return std::make_unique<WriteTestProgramToBank>(state, name, options);
    case ScriptId::RestoreBankBackup:
        return std::make_unique<RestoreBankBackup>(state, name, options);
    }
    return nullptr;
}

} // namespace nl2x::probe
