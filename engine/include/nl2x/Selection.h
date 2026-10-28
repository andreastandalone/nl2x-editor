#pragma once

// Selecting stored sounds (spec #1, "Selection"; user stories 31–33): a Program or Percussion Kit
// of a Program Bank for one Slot, or a Performance for the whole synth, by Bank Select (CC32) and
// Program Change. The MIDI numbering is in HardwareAssumptions.h; these types use the numbers
// the synth shows.

#include "nl2x/Slot.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace nl2x {

// A stored Program or Percussion Kit in a Program Bank.
struct ProgramSelection {
    enum class Kind : std::uint8_t { Program, PercussionKit };

    Kind kind = Kind::Program;
    // As shown on the synth: 0–9 (Kits only in Banks 0–3).
    int bank = 0;
    // Program 1–99, or Kit P0–P9 as 0–9.
    int number = 1;

    friend bool operator==(const ProgramSelection&, const ProgramSelection&) = default;
};

// A stored Performance: Bank 1–4, number 0–99 (shown as A0–L9, performanceName()).
struct PerformanceSelection {
    int bank = 1;
    int number = 0;

    friend bool operator==(const PerformanceSelection&, const PerformanceSelection&) = default;
};

bool isValid(const ProgramSelection& selection);
bool isValid(const PerformanceSelection& selection);

// The name the synth shows for Performance 0–99 of a Bank: "A0" … "L9", skipping the letters I
// and K [OFF-2X p.16, 32]. Empty outside 0–99.
std::string performanceName(int number);

// The Bank Select (CC32) value and Program Change number that select a stored sound, or empty
// when it does not exist; and back (empty for numbers that name no stored sound).
struct BankAndProgramChange {
    std::uint8_t bankSelect;
    std::uint8_t programChange;

    friend bool operator==(const BankAndProgramChange&, const BankAndProgramChange&) = default;
};
std::optional<BankAndProgramChange> midiSelection(const ProgramSelection& selection);
std::optional<BankAndProgramChange> midiSelection(const PerformanceSelection& selection);
std::optional<ProgramSelection> programSelectionFromMidi(int bankSelect, int programChange);
std::optional<PerformanceSelection> performanceSelectionFromMidi(int bankSelect, int programChange);

// What became of a selection made by the user.
enum class SelectionResult : std::uint8_t {
    // Bank Select and Program Change sent; the Edit Buffers are read again shortly.
    Sent,
    // Nothing sent: no such Program, Kit or Performance.
    RefusedInvalid,
    // Nothing sent: the Slot's channel is off, so it hears no Program Change.
    RefusedSlotChannelOff,
    // Nothing sent: the Slot is on the Global Channel, where a Program Change selects a
    // Performance instead [OFF-2X p.76].
    RefusedSlotOnGlobalChannel,
    // Nothing sent: the Global Channel is not known yet (not connected).
    RefusedNoGlobalChannel,
    // Nothing sent: the synth's memory is being read or written (MemoryWriter.h). A Program Change
    // would change the Bank selected on the synth, where Bank dumps are stored [OFF-2X p.78].
    RefusedMemoryBusy,
};

// What the plugin shows as selected and keeps in the project: the last selection made from the
// plugin or followed from the synth, per Slot, and the last Performance.
struct SelectionState {
    std::array<std::optional<ProgramSelection>, 4> programs;
    std::optional<PerformanceSelection> performance;

    friend bool operator==(const SelectionState&, const SelectionState&) = default;
};

} // namespace nl2x
