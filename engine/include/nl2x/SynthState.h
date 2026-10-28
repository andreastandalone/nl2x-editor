#pragma once

// What the editor knows about the synth: the public, read-only view of the engine's Synth model
// for the GUI (spec #1, "Synth model").

#include "nl2x/Program.h"

#include <cstdint>
#include <optional>

namespace nl2x {

enum class SlotContent : std::uint8_t {
    // Not read yet, or the synth never answered for this Slot.
    Unknown,
    Program,
    // The Slot plays a Percussion Kit; Kit editing is a later spec, so its values are not kept.
    PercussionKit,
};

struct SlotState {
    SlotContent content = SlotContent::Unknown;
    // The Slot's Program Edit Buffer, as last read from the synth and edited since; set exactly
    // when content is Program.
    std::optional<Program> program;
};

enum class ConnectionStatus : std::uint8_t {
    // No connection attempt yet.
    NotConnected,
    // Asking every possible Global Channel in turn which one the synth answers on.
    SearchingGlobalChannel,
    // The Global Channel is known; Slot Edit Buffers are being read.
    ReadingSlots,
    // The Global Channel is known and the last reads were answered.
    Connected,
    // The synth did not answer: no Global Channel answered, or a read on the known one timed out.
    NoAnswer,
};

} // namespace nl2x
