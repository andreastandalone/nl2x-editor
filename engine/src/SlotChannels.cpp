#include "nl2x/SlotChannels.h"

#include "nl2x/HardwareAssumptions.h"

namespace nl2x {

namespace {

constexpr int kMidiChannelCount = 16;

// "Slot A", "Slots A and B", "Slots A, B and D".
std::string slotList(const std::vector<Slot>& slots)
{
    std::string text = slots.size() == 1 ? "Slot " : "Slots ";
    for (std::size_t i = 0; i < slots.size(); ++i) {
        if (i > 0)
            text += i + 1 == slots.size() ? " and " : ", ";
        text += slotLetter(slots[i]);
    }
    return text;
}

std::string shownChannel(MidiChannel channel)
{
    return std::to_string(channel + 1);
}

} // namespace

std::vector<SlotChannelWarning> slotChannelWarnings(const SlotChannelArray& channels, const SlotLayerArray& layer)
{
    std::vector<SlotChannelWarning> warnings;

    for (int channel = 0; channel < kMidiChannelCount; ++channel) {
        std::vector<Slot> sharing;
        for (auto slot : kAllSlots)
            if (channels[slotIndex(slot)] == channel)
                sharing.push_back(slot);
        if (sharing.size() > 1)
            warnings.push_back({ SlotChannelWarning::Kind::SharedChannel, static_cast<MidiChannel>(channel), sharing });
    }

    if constexpr (hardware::kLayeredSlotsAlsoReceiveOnLeftmostActiveSlotChannel) {
        std::optional<Slot> leftmost;
        for (auto slot : kAllSlots)
            if (layer[slotIndex(slot)] && ! leftmost)
                leftmost = slot;
        const auto leftmostChannel = leftmost ? channels[slotIndex(*leftmost)] : std::nullopt;
        if (leftmostChannel) {
            std::vector<Slot> slots { *leftmost };
            for (auto slot : kAllSlots) {
                const auto& channel = channels[slotIndex(slot)];
                // A Slot already on that channel is covered by the shared-channel warning; a Slot
                // that is off does not listen at all.
                if (slot != *leftmost && layer[slotIndex(slot)] && channel && channel != leftmostChannel)
                    slots.push_back(slot);
            }
            if (slots.size() > 1)
                warnings.push_back({ SlotChannelWarning::Kind::Layer, *leftmostChannel, slots });
        }
    }

    return warnings;
}

std::string slotChannelWarningText(const SlotChannelWarning& warning)
{
    const auto channel = shownChannel(warning.channel);
    if (warning.kind == SlotChannelWarning::Kind::SharedChannel) {
        const auto affected = warning.slots.size() == 2 ? std::string("both") : std::string("all of them");
        return slotList(warning.slots) + " share MIDI channel " + channel + ": a knob change affects " + affected + ".";
    }

    const std::vector<Slot> others(warning.slots.begin() + 1, warning.slots.end());
    const std::string leftmost(1, slotLetter(warning.slots.front()));
    return slotList(others) + (others.size() == 1 ? " is" : " are") + " layered with Slot " + leftmost
           + " and also receive" + (others.size() == 1 ? "s" : "") + " on its MIDI channel " + channel
           + ": a knob change on Slot " + leftmost + " affects " + (others.size() == 1 ? "it" : "them") + " too.";
}

} // namespace nl2x
