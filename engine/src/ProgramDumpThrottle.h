#pragma once

#include "nl2x/Program.h"
#include "nl2x/ProgramParameters.h"
#include "nl2x/Slot.h"
#include "nl2x/TimeMs.h"

#include <array>
#include <cstdint>
#include <map>
#include <optional>

namespace nl2x {

// Edits of a Slot's Edit Buffer that travel as whole Program dumps (#7: Morph Amounts, and the
// ring-mod bit when CC35 does not reach it), and the pacing of those dumps: at most one per Slot
// per kProgramDumpIntervalMs, each Slot on its own. Leading and trailing: an edit after a quiet
// interval may go out at once, edits within the interval are coalesced into one dump when it has
// passed. Private to the engine; Engine builds the dump from the Slot's whole Edit Buffer and
// decides when the connection is free for it.
class ProgramDumpThrottle {
public:
    // `byte` was written at `parameter` of the Slot's Edit Buffer and has not been sent yet.
    void edited(Slot slot, ProgramParameter parameter, std::int8_t byte);

    bool hasUnsentEdits(Slot slot) const { return ! perSlot[slotIndex(slot)].unsent.empty(); }
    // A Program read from the synth replaces the Edit Buffer: edits not sent yet are kept on top of
    // it, since the user made them after the request went out.
    void applyUnsentEdits(Slot slot, Program& program) const;
    // The Slot's Edit Buffer was replaced (a selection, an Audition, a Kit read, a new connection):
    // its unsent edits no longer apply.
    void dropUnsentEdits(Slot slot) { perSlot[slotIndex(slot)].unsent.clear(); }
    void dropAllUnsentEdits();

    // Whether the Slot's dump may go out at `timeMs`: it has unsent edits and its last dump went
    // out at least kProgramDumpIntervalMs ago.
    bool isDue(Slot slot, TimeMs timeMs) const;
    // The Slot's dump went out at `timeMs`, carrying every unsent edit.
    void sent(Slot slot, TimeMs timeMs);

private:
    struct SlotDumps {
        // Bytes edited and not yet sent, by decoded offset.
        std::map<std::size_t, std::int8_t> unsent;
        std::optional<TimeMs> lastDumpMs;
    };
    std::array<SlotDumps, 4> perSlot;
};

} // namespace nl2x
