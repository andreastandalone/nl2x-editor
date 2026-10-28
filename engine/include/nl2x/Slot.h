#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace nl2x {

// One of the synth's four parts A–D (GLOSSARY.md).
enum class Slot : std::uint8_t { A, B, C, D };

inline constexpr std::array<Slot, 4> kAllSlots { Slot::A, Slot::B, Slot::C, Slot::D };

constexpr std::size_t slotIndex(Slot slot) { return static_cast<std::size_t>(slot); }

// "A" … "D", as printed on the synth's panel.
char slotLetter(Slot slot);

// MIDI channel as carried in the status byte: 0–15 for the channels shown as 1–16.
using MidiChannel = std::uint8_t;

} // namespace nl2x
