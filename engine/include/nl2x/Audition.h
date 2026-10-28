#pragma once

// Audition (GLOSSARY.md): sending a Program, Percussion Kit or Performance from the Library to an
// Edit Buffer to hear it, without storing it in any Bank (spec #1, user stories 51 and 57).

#include "nl2x/Slot.h"

#include <cstdint>
#include <string>

namespace nl2x {

enum class AuditionResult : std::uint8_t {
    // Accepted: the Edit Buffer dump goes out on a later advanceTime() (or at once when nothing is
    // waiting for a reply).
    Sent,
    // Refused, nothing sent: no Global Channel is known, so the synth would not listen.
    RefusedNotConnected,
    // Refused: the Slot has not been read from the synth, so it is not known whether it plays a
    // Program or a Percussion Kit, and the synth ignores a dump of the other kind [OFF-2X p.30].
    RefusedSlotNotRead,
    // Refused: a Program is received only into a Slot playing a Program [OFF-2X p.30].
    RefusedSlotPlaysPercussionKit,
    // Refused: a Percussion Kit is received only into a Slot playing a Percussion Kit [OFF-2X p.30].
    RefusedSlotPlaysProgram,
};

// What the user is told about a refusal, e.g. "Slot B is playing a Percussion Kit: …". Empty for
// Sent.
std::string auditionRefusalText(AuditionResult result, Slot slot);

} // namespace nl2x
