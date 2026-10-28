#include "SynthConnection.h"

#include "nl2x/HardwareAssumptions.h"

#include <utility>

namespace nl2x {

SynthConnection::SynthConnection(MidiOutput& midiOutput)
    : output(midiOutput)
{
}

void SynthConnection::enqueue(std::vector<std::uint8_t> request, Accepts accepts, Done done, TimeMs timeMs)
{
    queue.push_back({ std::move(request), std::move(accepts), std::move(done) });
    sendNextIfDue(timeMs);
}

void SynthConnection::clear()
{
    queue.clear();
    waiting.reset();
}

bool SynthConnection::isIdle(TimeMs timeMs) const
{
    return ! waiting && queue.empty() && (! nextSendTime || timeMs >= *nextSendTime);
}

void SynthConnection::sendWithoutReply(std::span<const std::uint8_t> message, TimeMs timeMs)
{
    output.send(message);
    nextSendTime = timeMs + hardware::kRequestSpacingMs;
}

void SynthConnection::handleIncomingMidi(std::span<const std::uint8_t> message, TimeMs timeMs)
{
    if (waiting && waiting->accepts(message))
        finishWaiting(message, timeMs);
}

void SynthConnection::advanceTime(TimeMs timeMs)
{
    if (waiting && timeMs - waitingSince >= hardware::kRequestReplyTimeoutMs)
        finishWaiting(std::nullopt, timeMs);
    sendNextIfDue(timeMs);
}

void SynthConnection::finishWaiting(std::optional<std::span<const std::uint8_t>> reply, TimeMs timeMs)
{
    // Moved out first: Done may queue or clear requests.
    auto finished = std::move(*waiting);
    waiting.reset();
    nextSendTime = timeMs + hardware::kRequestSpacingMs;
    finished.done(reply, timeMs);
}

void SynthConnection::sendNextIfDue(TimeMs timeMs)
{
    if (waiting || queue.empty() || (nextSendTime && timeMs < *nextSendTime))
        return;

    waiting = std::move(queue.front());
    queue.pop_front();
    waitingSince = timeMs;
    output.send(waiting->message);
}

} // namespace nl2x
