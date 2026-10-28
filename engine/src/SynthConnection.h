#pragma once

#include "nl2x/MidiOutput.h"
#include "nl2x/TimeMs.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace nl2x {

// Request and reply with the synth: one request at a time, each waiting for its reply or a
// timeout, with a gap before the next (HardwareAssumptions.h). The synth's replies carry no
// reliable reference to their request (research §3), so a reply is credited to the request that
// is waiting when it arrives, if that request accepts it.
class SynthConnection {
public:
    // Whether an incoming message answers the waiting request.
    using Accepts = std::function<bool(std::span<const std::uint8_t> message)>;
    // Called once per request: with the reply, or with nothing when the request timed out.
    // It may queue further requests or clear the queue.
    using Done = std::function<void(std::optional<std::span<const std::uint8_t>> reply, TimeMs timeMs)>;

    explicit SynthConnection(MidiOutput& output);

    // Sent at once when nothing is waiting and the gap since the last request has passed;
    // otherwise on a later advanceTime().
    void enqueue(std::vector<std::uint8_t> request, Accepts accepts, Done done, TimeMs timeMs);
    // Forgets every queued and waiting request without calling their Done.
    void clear();

    // Nothing queued or waiting, and the gap after the last message has passed: a message that
    // expects no reply may go out now.
    bool isIdle(TimeMs timeMs) const;
    // Sends a message that expects no reply (a dump) and keeps the usual gap before the next one.
    // Call only when isIdle().
    void sendWithoutReply(std::span<const std::uint8_t> message, TimeMs timeMs);

    void handleIncomingMidi(std::span<const std::uint8_t> message, TimeMs timeMs);
    void advanceTime(TimeMs timeMs);

private:
    struct Request {
        std::vector<std::uint8_t> message;
        Accepts accepts;
        Done done;
    };

    void finishWaiting(std::optional<std::span<const std::uint8_t>> reply, TimeMs timeMs);
    void sendNextIfDue(TimeMs timeMs);

    MidiOutput& output;
    std::deque<Request> queue;
    std::optional<Request> waiting;
    TimeMs waitingSince = 0;
    // When the next request may go out; empty before the first one.
    std::optional<TimeMs> nextSendTime;
};

} // namespace nl2x
