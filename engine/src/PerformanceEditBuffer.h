#pragma once

#include "nl2x/Performance.h"
#include "nl2x/SlotChannels.h"
#include "nl2x/SynthState.h"
#include "nl2x/TimeMs.h"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace nl2x {

// The editor's copy of the synth's Performance Edit Buffer (#5): the Performance last read, the
// user's edits on top of it, and the coalescing of those edits into Performance dumps. Private to
// the engine; Engine owns the requests and decides when a dump may go out.
class PerformanceEditBuffer {
public:
    PerformanceStatus status() const { return readStatus; }
    // The Performance last read, with the edits made since; empty until one has been read.
    const std::optional<Performance>& performance() const { return current; }

    // Whether a message is a reply to the Performance Edit Buffer request: a Performance dump,
    // matched by size (never by header) and only when it decodes without loss.
    static bool isReply(std::span<const std::uint8_t> message);

    // A new connection: what was read from the earlier one no longer applies.
    void forget();
    void startReading() { readStatus = PerformanceStatus::Reading; }
    // The synth's Performance replaces ours; edits not sent yet are kept on top of it, since the
    // user made them after the request went out.
    void applyReply(std::span<const std::uint8_t> reply);
    void readFailed() { readStatus = PerformanceStatus::NoAnswer; }

    // `value` in the setting's documented range; out-of-range user values are brought into it.
    PerformanceEditResult edit(PerformanceSetting setting, Slot slot, int value);

    // The Performance dump to send at `timeMs`, if an edit is waiting and the last dump went out at
    // least kPerformanceDumpIntervalMs ago. Its Programs are the Slots' Edit Buffers where a Slot
    // plays a Program; the bytes read from the synth otherwise (a Kit reference, or unknown).
    std::optional<std::vector<std::uint8_t>> takeDueDump(TimeMs timeMs, GlobalChannel globalChannel,
                                                         const std::array<SlotState, 4>& slots);

    // The Slot Channel stored in the Performance; empty when not read yet or off.
    std::optional<MidiChannel> slotChannel(Slot slot) const;
    // Which Slots are in the Layer (activated); none until read.
    SlotLayerArray layer() const;

private:
    std::optional<Performance> current;
    PerformanceStatus readStatus = PerformanceStatus::NotRead;
    // Bytes edited and not yet sent, by decoded offset.
    std::map<std::size_t, std::int8_t> unsentBytes;
    std::optional<TimeMs> lastDumpMs;
};

} // namespace nl2x
