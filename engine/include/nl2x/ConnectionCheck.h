#pragma once

// The Connection check (spec #1, user stories 6–8): tests the link with the synth and explains
// each problem in plain English, with the setting to change on the synth in the manual's words.
// These are the public, read-only results; the engine runs the check (Engine::startConnectionCheck).

#include "nl2x/SysEx.h"
#include "nl2x/TimeMs.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace nl2x {

// What the check tests, in the order it shows them.
enum class ConnectionCheckItem : std::uint8_t {
    // Any MIDI at all arrives from the synth: the MIDI return cable is there.
    MidiFromSynth,
    // The synth answers SysEx requests.
    SysExAnswered,
    // The synth answers on the Global Channel the editor uses.
    GlobalChannel,
    // The synth transmits CCs (C on, under Shift + PROG/CTRL).
    CcsTransmitted,
    // The synth receives CCs (the same C setting).
    CcsReceived,
};

inline constexpr std::size_t kConnectionCheckItemCount = 5;

enum class ConnectionCheckOutcome : std::uint8_t {
    // Not reached yet.
    Pending,
    Running,
    Passed,
    Failed,
    // Could not be tested: an earlier test failed, or the user skipped the step.
    NotTested,
};

struct ConnectionCheckResult {
    ConnectionCheckItem item = ConnectionCheckItem::MidiFromSynth;
    ConnectionCheckOutcome outcome = ConnectionCheckOutcome::Pending;
    // Short name of the test, e.g. "MIDI from the synth".
    std::string title;
    // What was found, in plain English.
    std::string explanation;
    // What to do about it, in the manual's terms; empty when nothing needs doing.
    std::string fix;
};

enum class ConnectionCheckState : std::uint8_t {
    // Never started, or cancelled.
    Idle,
    Running,
    // Waiting for the user to do something on the synth (ConnectionCheckReport::userPrompt).
    // The step ends by itself when MIDI arrives or after kConnectionCheckUserStepTimeoutMs, or
    // when the user skips it.
    WaitingForUser,
    Finished,
};

// How long an interactive step waits for the user (play a key, turn a knob). A choice for the
// user, not a guess about the synth.
inline constexpr TimeMs kConnectionCheckUserStepTimeoutMs = 15000;

struct ConnectionCheckReport {
    ConnectionCheckState state = ConnectionCheckState::Idle;
    // Indexed by ConnectionCheckItem.
    std::array<ConnectionCheckResult, kConnectionCheckItemCount> results {};
    // What the user should do on the synth now; set exactly while WaitingForUser.
    std::string userPrompt;
    // When the interactive step gives up, on the caller's clock; valid while WaitingForUser.
    TimeMs userStepDeadline = 0;
    // The Global Channel the synth answered on during the check; empty if none did.
    std::optional<GlobalChannel> answeringGlobalChannel;

    const ConnectionCheckResult& result(ConnectionCheckItem item) const
    {
        return results[static_cast<std::size_t>(item)];
    }
    // Every test either passed or was not needed.
    bool allPassed() const;
};

} // namespace nl2x
