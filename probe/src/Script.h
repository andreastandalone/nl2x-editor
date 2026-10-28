#pragma once

// Internal to the probe: the base of every script and the factory that builds them.

#include "nl2x/probe/ProbeSession.h"

#include <memory>
#include <optional>
#include <string>

namespace nl2x::probe {

// How long a script waits for the synth to answer a request. A Percussion Kit dump (1063 bytes)
// takes about 340 ms on a MIDI cable; one second leaves room for the synth's own delay.
inline constexpr TimeMs kReplyTimeoutMs = 1000;

// "139 bytes, Program dump, dev 15, type 0, spec 0".
std::string describeSysEx(const ParsedSysEx& sysEx);

// A time-driven state machine. The session calls start() once, then handleIncomingMidi() and
// advanceTime() until finished(). Every script ends with one "SUMMARY <name>: …" line.
class ProbeSession::Script {
public:
    Script(State& sessionState, std::string_view scriptName) : session(sessionState), name(scriptName) {}
    virtual ~Script() = default;

    virtual void start(TimeMs nowMs) = 0;
    // `sysEx` is the parsed message when it is a SysEx message, nullptr otherwise.
    virtual void handleIncomingMidi(std::span<const std::uint8_t> message, const ParsedSysEx* sysEx, TimeMs nowMs) = 0;
    virtual void advanceTime(TimeMs nowMs) = 0;

    // Why the script cannot start (nothing is sent then); empty when it can.
    virtual std::string preconditionFailure() const { return {}; }

    void stop();
    bool finished() const { return done; }

protected:
    // What the script found, for its SUMMARY line.
    virtual std::string summary() const = 0;
    // Lines reported before the SUMMARY line.
    virtual void reportDetails() {}
    // Scripts that run until the user stops them.
    virtual bool endsWhenStopped() const { return false; }

    void complete();
    void fail(std::string_view reason);

    void send(std::span<const std::uint8_t> message) { session.output.send(message); }
    void report(std::string_view line) { session.report(line); }
    GlobalChannel globalChannel() const { return session.globalChannel; }

    // Sends a request and waits up to kReplyTimeoutMs for a SysEx reply of `kind` with valid data.
    // The wait starts before the message is sent, so a reply delivered at once is not missed.
    void sendRequest(std::span<const std::uint8_t> request, SysExKind kind, TimeMs nowMs);
    bool awaitingReply() const { return awaitedKind.has_value(); }
    // The reply, when `sysEx` is the one awaited.
    std::optional<ParsedSysEx> acceptReply(const ParsedSysEx* sysEx);
    // True once, when the awaited reply did not come in time.
    bool replyTimedOut(TimeMs nowMs);

    State& session;
    const std::string name;

private:
    std::optional<SysExKind> awaitedKind;
    TimeMs replyDeadline = 0;
    bool done = false;
};

std::unique_ptr<ProbeSession::Script> makeScript(ScriptId id, ProbeSession::State& state, const ScriptOptions& options);

} // namespace nl2x::probe
