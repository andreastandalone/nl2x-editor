#include "SimulatedNordLead2X.h"

#include "nl2x/HardwareAssumptions.h"

#include <algorithm>
#include <utility>

namespace nl2x::test {

namespace {

// CC number → decoded Program offset, from the CC map in research §2 [OFF-2X p.103–104,
// 111–112]. Grows with the engine's parameter table (#6).
constexpr std::array<std::pair<std::uint8_t, std::size_t>, 1> kCcToProgramOffset { {
    { 74, 3 }, // Filter Cutoff
} };

} // namespace

SimulatedNordLead2X::SimulatedNordLead2X()
{
    for (auto slot : kAllSlots)
        slots[slotIndex(slot)].channel = hardware::kDefaultSlotChannels[slotIndex(slot)];
}

void SimulatedNordLead2X::receive(std::span<const std::uint8_t> message)
{
    if (message.size() == 3 && (message[0] & 0xF0) == 0xB0)
        handleControlChange(static_cast<MidiChannel>(message[0] & 0x0F), message[1], message[2]);
}

void SimulatedNordLead2X::turnPanelControl(Slot slot, std::uint8_t cc, std::uint8_t value)
{
    const auto& state = slots[slotIndex(slot)];
    if (! state.channel)
        return;

    handleControlChange(*state.channel, cc, value);
    const std::array<std::uint8_t, 3> message { static_cast<std::uint8_t>(0xB0 | *state.channel), cc, value };
    transmit(message);
}

void SimulatedNordLead2X::handleControlChange(MidiChannel channel, std::uint8_t cc, std::uint8_t value)
{
    const auto* entry = std::find_if(kCcToProgramOffset.begin(), kCcToProgramOffset.end(),
                                     [cc](const auto& e) { return e.first == cc; });
    if (entry == kCcToProgramOffset.end())
        return;

    // Every Slot on the channel reacts [OFF-2X p.77].
    for (auto& state : slots)
        if (state.channel == channel)
            state.editBuffer[entry->second] = static_cast<std::int8_t>(value);
}

void SimulatedNordLead2X::transmit(std::span<const std::uint8_t> message)
{
    if (midiOut != nullptr)
        midiOut->send(message);
}

} // namespace nl2x::test
