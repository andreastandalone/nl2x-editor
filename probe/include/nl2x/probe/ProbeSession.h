#pragma once

// The hardware discovery probe (#3): scripted requests and dumps sent to the synth, with every
// reply reported, so the session in docs/hardware-discovery-plan.md can settle what the manual
// gets wrong. Plain C++ with the engine's seam: messages out through a MidiOutput, messages in
// through handleIncomingMidi(), time through advanceTime(). It never reads a clock, so every
// script is tested against the simulated synth.
//
// Not part of the editor: the standalone app shows it in a Probe window; the engine never uses it.

#include "nl2x/Engine.h"
#include "nl2x/MidiOutput.h"
#include "nl2x/Slot.h"
#include "nl2x/SysEx.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nl2x::probe {

// One per button of the Probe window, in the order of the plan.
enum class ScriptId : std::uint8_t {
    AllControllersRequest,
    ProbeGlobalChannel,
    RequestSlotsType14,
    RequestSlotsType10,
    RequestKitType10Spec16,
    RequestKitType14Spec16,
    RequestKitType14Spec0,
    ScanBankRequests,
    RequestPerformanceEditBuffer,
    RequestPerformanceBank1Location0,
    SendProgramCutoffLowHigh,
    SendProgramMorphOnCutoff,
    RestoreSlotAProgram,
    SendPerformanceToggleSplit,
    WatchCcs,
    SendCcSweep,
    PacingTest,
    StressProgramDumps,
    BackUpBank,
    WriteTestProgramToBank,
    RestoreBankBackup,
};

struct ScriptInfo {
    ScriptId id;
    // The button's label; the plan names scripts by it.
    std::string_view name;
    std::string_view description;
    // Scripts that store into the synth's Banks. The window asks for confirmation first.
    bool writesToMemory;
};

std::span<const ScriptInfo> scripts();
const ScriptInfo& scriptInfo(ScriptId id);

// Settings typed in the Probe window, read when a script starts.
struct ScriptOptions {
    // Send CC sweep.
    std::uint8_t sweepCc = 35;
    MidiChannel sweepChannel = 0;
    // The request type that returned the target Bank's Programs in "Scan Bank requests" (plan
    // step 5). Bank scripts refuse to run without it.
    std::optional<std::uint8_t> bankRequestType;
    // The type byte of the Program dumps written to the Bank. The synth ignores it and stores into
    // the Bank selected on its panel [OFF-2X p.78], but it must be a Bank dump type (1–10).
    std::uint8_t bankDumpType = 4;
};

// The 99 Programs of one Bank as the synth sent them, in location order.
struct BankBackup {
    std::uint8_t requestType = 0;
    std::vector<std::vector<std::uint8_t>> replies;
};

class ProbeSession {
public:
    // Each line the scripts report (notes and SUMMARY lines), without a timestamp.
    using Report = std::function<void(std::string_view line)>;

    ProbeSession(MidiOutput& output, Report report);
    ~ProbeSession();

    // Used by every script that does not probe all Global Channels itself.
    void setGlobalChannel(GlobalChannel channel) { currentGlobalChannel = channel; }
    GlobalChannel globalChannel() const { return currentGlobalChannel; }

    // Starts a script; false (with the reason reported) when another script is running or the
    // script's preconditions are not met.
    bool start(ScriptId id, const ScriptOptions& options, TimeMs nowMs);
    // Ends the running script early; it reports what it found so far.
    void stop(TimeMs nowMs);
    bool isRunning() const { return running != nullptr; }

    void handleIncomingMidi(std::span<const std::uint8_t> message, TimeMs nowMs);
    void advanceTime(TimeMs nowMs);

    // Only complete backups (all 99 locations answered) are kept; the latest replaces earlier ones.
    const std::optional<BankBackup>& bankBackup() const { return state.bankBackup; }
    // Called when a complete backup has been captured, so the window can save it to a file at once.
    std::function<void(const BankBackup&)> onBankBackupCaptured;

    // The state scripts share across runs within one session.
    struct State {
        MidiOutput& output;
        Report report;
        GlobalChannel& globalChannel;
        std::function<void(const BankBackup&)>& onBankBackupCaptured;
        // The Slot A Program as it was before the first "Send Program to Slot A" script changed it.
        std::optional<std::vector<std::int8_t>> slotAOriginal;
        // The last Program the probe sent to Slot A, to tell whether Slot A still holds our change.
        std::optional<std::vector<std::int8_t>> slotALastSent;
        std::optional<BankBackup> bankBackup;
    };

    class Script;

private:
    void finishIfDone();

    GlobalChannel currentGlobalChannel;
    State state;
    std::unique_ptr<Script> running;
};

} // namespace nl2x::probe
