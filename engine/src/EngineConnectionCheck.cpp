// The Engine's Connection check, manual Global Channel and request suspension (#9), kept apart
// from Engine.cpp.

#include "nl2x/Engine.h"

#include "ConnectionCheck.h"
#include "SynthConnection.h"

#include <array>

namespace nl2x {

void Engine::setManualGlobalChannel(std::optional<GlobalChannel> channel)
{
    if (manualChannel == channel)
        return;
    manualChannel = channel;
    if (channel && foundGlobalChannel && foundGlobalChannel != channel)
        foundGlobalChannel.reset();
    changed();
}

void Engine::setRequestsSuspended(bool newSuspended, TimeMs timeMs)
{
    if (suspended == newSuspended)
        return;
    suspended = newSuspended;
    if (suspended)
        check->cancel();
    connection->setSuspended(suspended, timeMs);
    changed();
}

void Engine::startConnectionCheck(TimeMs timeMs)
{
    if (suspended)
        return;
    std::array<std::optional<MidiChannel>, 4> channels;
    for (auto slot : kAllSlots)
        channels[slotIndex(slot)] = slotChannel(slot);
    check->start({ manualChannel, foundGlobalChannel, channels }, timeMs);
}

void Engine::skipConnectionCheckStep(TimeMs timeMs)
{
    check->skipUserStep(timeMs);
}

void Engine::cancelConnectionCheck()
{
    check->cancel();
}

const ConnectionCheckReport& Engine::connectionCheck() const
{
    return check->report();
}

} // namespace nl2x
