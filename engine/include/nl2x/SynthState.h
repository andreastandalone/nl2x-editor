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

    friend bool operator==(const SlotState&, const SlotState&) = default;
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

// The editor's copy of the synth's Performance Edit Buffer (#5).
enum class PerformanceStatus : std::uint8_t {
    // Never read since connecting: Slot Channels are the defaults or manual overrides, and
    // Performance edits are refused.
    NotRead,
    // A Performance Edit Buffer request (type 40) is queued or waiting for its reply.
    Reading,
    // The last request was answered.
    Read,
    // The last request timed out; a Performance read earlier is kept.
    NoAnswer,
};

// What became of a Performance setting edit.
enum class PerformanceEditResult : std::uint8_t {
    // Kept, and sent with the next Performance dump.
    Accepted,
    // Refused, nothing sent: the Performance has not been read from the synth, so a dump would
    // overwrite the synth's settings with guesses.
    RefusedNotRead,
    // Refused: the synth ignores this setting in a received dump ("not received", or read-only in
    // the editor; PerformanceSettingAccess).
    RefusedNotEditable,
};

} // namespace nl2x
