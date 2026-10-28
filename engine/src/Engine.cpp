#include "nl2x/Engine.h"

#include "nl2x/HardwareAssumptions.h"

#include <algorithm>

namespace nl2x {

namespace {

constexpr std::uint8_t kControlChange = 0xB0;

} // namespace

Engine::Engine(MidiOutput& midiOutput)
    : output(midiOutput)
{
    for (auto slot : kAllSlots)
        slotChannels[slotIndex(slot)] = hardware::kDefaultSlotChannels[slotIndex(slot)];
}

void Engine::handleIncomingMidi(std::span<const std::uint8_t> /*message*/, TimeMs /*timeMs*/)
{
    // Nothing from the synth is understood yet: reading Slots (#4) and incoming CCs (#6) come next.
}

void Engine::advanceTime(TimeMs /*timeMs*/)
{
    // Nothing is time-driven yet: Program dump throttling (#7) and request timeouts (#4) come next.
}

void Engine::setParameter(Slot slot, ProgramParameter parameter, int value)
{
    const auto& info = programParameterInfo(parameter);
    const auto& channel = slotChannels[slotIndex(slot)];
    if (! info.cc || ! channel)
        return;

    // A CC data byte has 7 bits.
    const auto data = static_cast<std::uint8_t>(std::clamp(value, 0, 127));
    const std::array<std::uint8_t, 3> message { static_cast<std::uint8_t>(kControlChange | *channel), *info.cc, data };
    output.send(message);
}

} // namespace nl2x
