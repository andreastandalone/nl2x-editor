#pragma once

// Which Slots a knob change reaches: Slots that share a MIDI channel all react to its CCs
// [OFF-2X p.77], and Slots in a Layer also receive on the leftmost active Slot's channel
// [OFF-2X p.76–77] (research §4.7). The editor sends each Slot's CCs on that Slot's channel, so
// both cases mean an edit of one Slot changes others too; the GUI warns about them.

#include "nl2x/Slot.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace nl2x {

// Each Slot's channel (empty = off) and whether it is in the Layer (activated).
using SlotChannelArray = std::array<std::optional<MidiChannel>, 4>;
using SlotLayerArray = std::array<bool, 4>;

struct SlotChannelWarning {
    enum class Kind : std::uint8_t {
        // `slots` (two or more, in A–D order) are all set to `channel`.
        SharedChannel,
        // `slots[0]` is the leftmost active Slot, on `channel`; the other Slots of the Layer, on
        // channels of their own, also receive on `channel`.
        Layer,
    };

    Kind kind;
    MidiChannel channel;
    std::vector<Slot> slots;

    friend bool operator==(const SlotChannelWarning&, const SlotChannelWarning&) = default;
};

// Shared channels first (by channel), then the Layer warning.
std::vector<SlotChannelWarning> slotChannelWarnings(const SlotChannelArray& channels, const SlotLayerArray& layer);

// "Slots A and B share MIDI channel 1: a knob change affects both."
std::string slotChannelWarningText(const SlotChannelWarning& warning);

} // namespace nl2x
