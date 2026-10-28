#pragma once

// Morph Amounts (GLOSSARY.md, #7): how far velocity or the mod wheel moves each of the 25
// morphable parameters, offsets 25–49 of the Program, signed −128…127, 0 = off [OFF-2X p.111].
// They have no CC (research §2): an edit reaches the synth as a whole Program dump to the Slot's
// Edit Buffer, throttled per Slot (HardwareAssumptions.h, kProgramDumpIntervalMs).

#include "nl2x/Program.h"
#include "nl2x/ProgramParameters.h"
#include "nl2x/Slot.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace nl2x {

inline constexpr int kMorphAmountMin = -128;
inline constexpr int kMorphAmountMax = 127;

// The 25 base parameters that have a Morph Amount, in offset order.
std::span<const ProgramParameter> morphableParameters();

// The Morph Amount byte of a base parameter; empty when it has none.
std::optional<ProgramParameter> morphAmountOf(ProgramParameter baseParameter);

// The base parameter's Morph Amount in `program`, exactly as stored. `baseParameter` must be
// morphable.
int morphAmountValue(const Program& program, ProgramParameter baseParameter);

// DAW-facing identity of a Slot's Morph Amount: "A_filterCutoffMorph" and "A: Filter Cutoff Morph".
// The ID never changes once released (automation and saved projects refer to it).
std::string morphDawParameterId(Slot slot, ProgramParameter baseParameter);
std::string morphDawParameterName(Slot slot, ProgramParameter baseParameter);

// What became of a Morph Amount edit.
enum class MorphEditResult : std::uint8_t {
    // Kept in the Slot's Edit Buffer and sent with the Slot's next Program dump.
    Accepted,
    // Refused, nothing sent: the parameter has no Morph Amount.
    RefusedNotMorphable,
    // Refused: the Slot has not been read, so a dump would overwrite the synth's sound with guesses.
    RefusedSlotNotRead,
    // Refused: the Slot plays a Percussion Kit, whose Edit Buffer a Program dump does not reach
    // [OFF-2X p.30] (Kit editing is a later spec).
    RefusedSlotPlaysPercussionKit,
    // Refused: no Global Channel is known, so the synth would not listen to a dump.
    RefusedNotConnected,
};

// What the user is told about a refusal, e.g. "Slot B is playing a Percussion Kit: …". Empty for
// Accepted.
std::string morphEditRefusalText(MorphEditResult result, Slot slot);

} // namespace nl2x
