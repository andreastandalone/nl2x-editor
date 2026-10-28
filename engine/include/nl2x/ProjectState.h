#pragma once

// Project recall (#8, spec #1 user stories 43–47): the sounds a DAW project keeps, as an opaque
// blob the plugin shell stores in its state. The engine writes and reads it (Engine::projectState,
// Engine::loadProjectState); the shell never looks inside.
//
// Format, version kProjectStateFormatVersion:
//   "NL2P"                      4 bytes, so that a foreign blob is never mistaken for one
//   version                     1 byte
//   4 × Slot (A–D):             content byte (0 = unknown, 1 = Program, 2 = Percussion Kit),
//                               followed by the Program's 66 bytes for a Program
//   Performance present         1 byte (0 or 1), followed by its 354 bytes when present
// Bytes are the signed SysEx values as two's complement, exactly as read: nothing is clamped, so
// unknown and out-of-range values round-trip (CODING_STANDARDS.md). A Percussion Kit is kept as
// "plays a Kit" only: the editor does not keep Kit data (#4), and a Performance holds Kits by
// reference only [OFF-2X p.31–32].

#include "nl2x/Performance.h"
#include "nl2x/SynthState.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace nl2x {

inline constexpr std::uint8_t kProjectStateFormatVersion = 1;

// The Synth model's sounds as a project keeps them.
struct ProjectSounds {
    std::array<SlotState, 4> slots;
    // The Performance Edit Buffer, if it had been read (with the edits made since).
    std::optional<Performance> performance;

    friend bool operator==(const ProjectSounds&, const ProjectSounds&) = default;
};

std::vector<std::uint8_t> encodeProjectState(const ProjectSounds& sounds);
// Empty for anything that is not exactly a blob of this format version: a newer or older version,
// a truncated or padded blob, an unknown content byte. A rejected blob is never partly used.
std::optional<ProjectSounds> decodeProjectState(std::span<const std::uint8_t> state);

// What became of a project state handed to Engine::loadProjectState.
enum class ProjectLoadResult : std::uint8_t {
    // Connected: the sounds are being sent to the Edit Buffers.
    Sending,
    // Not connected yet: the sounds are sent right after the Global Channel is found, instead of
    // reading the Slots from the synth.
    HeldUntilConnected,
    // Automatic sending is off: the sounds are shown until the synth is read, which happens at
    // once when connected and otherwise on connecting.
    NotSent,
    // Not a state this version understands: nothing changed, nothing sent; the synth is read as
    // usual.
    Rejected,
};

} // namespace nl2x
