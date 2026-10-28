#include "nl2x/probe/ProbeSession.h"

#include "Script.h"

#include "nl2x/HardwareAssumptions.h"

#include <array>
#include <cassert>
#include <utility>

namespace nl2x::probe {

namespace {

constexpr std::array kScripts {
    ScriptInfo { ScriptId::AllControllersRequest, "All Controllers Request A",
                 "All Controllers Request for Slot A (type 20, spec 0) to every dev 0–15, 300 ms apart; counts the "
                 "CCs that come back after each.",
                 false },
    ScriptInfo { ScriptId::ProbeGlobalChannel, "Probe Global Channel",
                 "Slot A Edit Buffer request (type 14, spec 0) to every dev 0–15, 300 ms apart; adopts the Global "
                 "Channel when exactly one dev answers.",
                 false },
    ScriptInfo { ScriptId::RequestSlotsType14, "Request Slot A–D (type 14)",
                 "Requests type 14, spec 0–3, one second apart.", false },
    ScriptInfo { ScriptId::RequestSlotsType10, "Request Slot A–D (type 10)",
                 "Requests type 10 (the documented Edit Buffer request), spec 0–3, one second apart.", false },
    ScriptInfo { ScriptId::RequestKitType10Spec16, "Request Kit Slot A (type 10, spec 16)",
                 "One request, type 10, spec 16.", false },
    ScriptInfo { ScriptId::RequestKitType14Spec16, "Request Kit Slot A (type 14, spec 16)",
                 "One request, type 14, spec 16.", false },
    ScriptInfo { ScriptId::RequestKitType14Spec0, "Request Kit Slot A (type 14, spec 0)",
                 "One request, type 14, spec 0.", false },
    ScriptInfo { ScriptId::ScanBankRequests, "Scan Bank requests",
                 "Requests types 11–24 × specs 0, 1, 98, 99, 108, 400 ms apart (read-only); lists every reply.",
                 false },
    ScriptInfo { ScriptId::RequestPerformanceEditBuffer, "Request Performance Edit Buffer (type 40)",
                 "One request, type 40, spec 0.", false },
    ScriptInfo { ScriptId::RequestPerformanceBank1Location0, "Request Performance Bank 1 location 0 (type 41)",
                 "One request, type 41, spec 0.", false },
    ScriptInfo { ScriptId::SendProgramCutoffLowHigh, "Send Program to Slot A: cutoff low/high",
                 "Reads Slot A, sends it back to Slot A's Edit Buffer with Filter Cutoff 0, then 3 s later with "
                 "Filter Cutoff 127.",
                 false },
    ScriptInfo { ScriptId::SendProgramMorphOnCutoff, "Send Program to Slot A: Morph Amount on Cutoff",
                 "Reads Slot A, sends it back with Filter Cutoff 20 and a Morph Amount of +100 on Filter Cutoff.",
                 false },
    ScriptInfo { ScriptId::RestoreSlotAProgram, "Restore Slot A Program",
                 "Sends the Slot A Program read before the first 'Send Program to Slot A' back to Slot A's Edit "
                 "Buffer.",
                 false },
    ScriptInfo { ScriptId::SendPerformanceToggleSplit, "Send Performance: toggle split",
                 "Reads the Performance Edit Buffer (type 40) and sends it back (type 30) with Keyboard Split "
                 "toggled.",
                 false },
    ScriptInfo { ScriptId::WatchCcs, "Watch CCs",
                 "Collects every CC from the synth until Stop, then prints the values seen per CC.", false },
    ScriptInfo { ScriptId::SendCcSweep, "Send CC sweep",
                 "Sends the chosen CC with values 0–127 on the chosen channel, 250 ms apart.", false },
    ScriptInfo { ScriptId::PacingTest, "Pacing test",
                 "Reads Slot A, then 20 Program dumps to Slot A's Edit Buffer at 0, 5, 10, 20 and 50 ms spacing, "
                 "each read back and compared; restores Slot A at the end.",
                 false },
    ScriptInfo { ScriptId::StressProgramDumps, "Stress: Program dumps",
                 "Reads Slot A, sends it back unchanged every 50 ms for 30 s, then reads it again and compares.",
                 false },
    ScriptInfo { ScriptId::BackUpBank, "Back up Bank",
                 "Reads all 99 Programs of the Bank with the chosen Bank request type (read-only) and keeps them "
                 "for this session.",
                 false },
    ScriptInfo { ScriptId::WriteTestProgramToBank, "Write test Program to Bank (writes to memory)",
                 "WRITES TO THE SYNTH'S MEMORY: stores a test Program at Program 99 of the Bank selected on the "
                 "synth, then reads it back. Needs a backup of that Bank from this session.",
                 true },
    ScriptInfo { ScriptId::RestoreBankBackup, "Restore Bank backup (writes to memory)",
                 "WRITES TO THE SYNTH'S MEMORY: sends the 99 backed-up Programs into the Bank selected on the synth, "
                 "100 ms apart, then reads them back and compares.",
                 true },
};

} // namespace

std::span<const ScriptInfo> scripts()
{
    return kScripts;
}

const ScriptInfo& scriptInfo(ScriptId id)
{
    const auto& info = kScripts[static_cast<std::size_t>(id)];
    assert(info.id == id && "kScripts must be in ScriptId order");
    return info;
}

std::string describeSysEx(const ParsedSysEx& sysEx)
{
    auto text = std::to_string(sysEx.size) + " bytes, " + std::string(sysExKindName(sysEx.kind));
    if (sysEx.kind == SysExKind::NotNordLead)
        return text;

    text += ", dev " + std::to_string(sysEx.globalChannel) + ", type " + std::to_string(sysEx.type) + ", spec "
            + std::to_string(sysEx.spec);
    if (! sysEx.values)
        text += ", data bytes are not nibbles";
    return text;
}

// ---------------------------------------------------------------------------------------------

void ProbeSession::Script::stop()
{
    if (done)
        return;
    reportDetails();
    report("SUMMARY " + name + ": " + summary() + (endsWhenStopped() ? "" : " (stopped before the end)"));
    done = true;
}

void ProbeSession::Script::complete()
{
    reportDetails();
    report("SUMMARY " + name + ": " + summary());
    done = true;
}

void ProbeSession::Script::fail(std::string_view reason)
{
    report("SUMMARY " + name + ": FAILED: " + std::string(reason));
    done = true;
}

void ProbeSession::Script::sendRequest(std::span<const std::uint8_t> request, SysExKind kind, TimeMs nowMs)
{
    awaitedKind = kind;
    replyDeadline = nowMs + kReplyTimeoutMs;
    send(request);
}

std::optional<ParsedSysEx> ProbeSession::Script::acceptReply(const ParsedSysEx* sysEx)
{
    if (! awaitedKind || sysEx == nullptr || sysEx->kind != *awaitedKind || ! sysEx->values)
        return std::nullopt;
    awaitedKind.reset();
    return *sysEx;
}

bool ProbeSession::Script::replyTimedOut(TimeMs nowMs)
{
    if (! awaitedKind || nowMs < replyDeadline)
        return false;
    awaitedKind.reset();
    return true;
}

// ---------------------------------------------------------------------------------------------

ProbeSession::ProbeSession(MidiOutput& output, Report report)
    : currentGlobalChannel(hardware::kFactoryGlobalChannel),
      state { output, std::move(report), currentGlobalChannel, onBankBackupCaptured, {}, {}, {} }
{
}

ProbeSession::~ProbeSession() = default;

bool ProbeSession::start(ScriptId id, const ScriptOptions& options, TimeMs nowMs)
{
    if (running != nullptr) {
        state.report("Not started: " + std::string(scriptInfo(id).name) + ": stop the running script first");
        return false;
    }

    auto script = makeScript(id, state, options);
    const auto name = std::string(scriptInfo(id).name);
    if (const auto failure = script->preconditionFailure(); ! failure.empty()) {
        state.report("Not started: " + name + ": " + failure);
        return false;
    }

    state.report("START " + name);
    running = std::move(script);
    running->start(nowMs);
    finishIfDone();
    return true;
}

void ProbeSession::stop(TimeMs)
{
    if (running == nullptr)
        return;
    running->stop();
    finishIfDone();
}

void ProbeSession::handleIncomingMidi(std::span<const std::uint8_t> message, TimeMs nowMs)
{
    std::optional<ParsedSysEx> sysEx;
    if (! message.empty() && message[0] == kSysExStart) {
        sysEx = parseSysEx(message);
        state.report("SysEx in: " + describeSysEx(*sysEx));
    }

    if (running != nullptr) {
        running->handleIncomingMidi(message, sysEx ? &*sysEx : nullptr, nowMs);
        finishIfDone();
    }
}

void ProbeSession::advanceTime(TimeMs nowMs)
{
    if (running != nullptr) {
        running->advanceTime(nowMs);
        finishIfDone();
    }
}

void ProbeSession::finishIfDone()
{
    if (running != nullptr && running->finished())
        running.reset();
}

} // namespace nl2x::probe
