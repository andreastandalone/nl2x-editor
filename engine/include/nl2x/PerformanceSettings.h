#pragma once

// The 90 Performance settings that follow the four Programs in a Performance (decoded offsets
// 264–353, research §3 "Performance layout" [OFF-2X p.112]). Names follow the manual's Shift
// function and panel terms [OFF-2X p.15–20, 63–77].

#include "nl2x/Slot.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace nl2x {

// One logical setting. Settings marked "[A–D]" in the manual hold one byte per Slot; they are
// addressed with a Slot, the others ignore it.
enum class PerformanceSetting : std::uint8_t {
    SlotChannel,                     // [A–D] 264
    Lfo1Sync,                        // [A–D] 268
    Lfo2Sync,                        // [A–D] 272
    FilterEnvTrig,                   // [A–D] 276
    FilterEnvTrigChannel,            // [A–D] 280
    FilterEnvTrigNote,               // [A–D] 284
    AmpEnvTrig,                      // [A–D] 288
    AmpEnvTrigChannel,               // [A–D] 292
    AmpEnvTrigNote,                  // [A–D] 296
    ExternalVelocityMorph,           // [A–D] 300
    ExternalVelocityMorphChannel,    // [A–D] 304
    ExternalVelocityMorphNote,       // [A–D] 308
    PitchBendRange,                  // 312
    UnisonDetune,                    // 313
    OutModeAB,                       // 314, low nibble
    OutModeCD,                       // 314, high nibble
    GlobalChannel,                   // 315
    ProgramChange,                   // 316
    ControlChange,                   // 317
    MasterTune,                      // 318
    PedalType,                       // 319
    LocalControl,                    // 320
    KeyboardOctaveShift,             // 321
    SelectedSlot,                    // 322
    ArpMidiOut,                      // 323
    Layer,                           // [A–D] 324, "channel_actived"
    ProgramNumber,                   // [A–D] 328
    ProgramBank,                     // [A–D] 332
    AftertouchAmount,                // [A–D] 336
    AftertouchDestination,           // [A–D] 340
    ControlPedalAmount,              // [A–D] 344
    ControlPedalDestination,         // [A–D] 348
    KeyboardSplit,                   // 352
    SplitPoint,                      // 353
};

// Where a setting lives inside its byte.
enum class PerformanceSettingBits : std::uint8_t {
    // The whole byte, signed.
    Byte,
    // Out Mode packs A/B and C/D into one byte [OFF-2X p.112]; each nibble is read unsigned.
    LowNibble,
    HighNibble,
};

enum class PerformanceSettingAccess : std::uint8_t {
    // Sent to the synth in a Performance dump when edited.
    Editable,
    // Received by the synth, but the editor does not offer to change it: the Program and Bank a
    // Slot was loaded from are references, the Program data in the Performance is what plays
    // [OFF-2X p.31–33], so changing the number alone would mislead.
    ReadOnly,
    // "not received!!!" [OFF-2X p.112]: global settings that the synth ignores in a received
    // Performance dump (research §4.4). Shown read-only, never sent as edits.
    NotReceived,
};

// How the GUI shows a value.
enum class PerformanceSettingDisplay : std::uint8_t {
    Number,
    // 0–15 shown as 1–16.
    MidiChannel,
    // A MIDI note number.
    Note,
};

struct PerformanceSettingInfo {
    PerformanceSetting id;
    // Never changes once released: DAW automation and saved projects refer to it.
    std::string_view key;
    // The manual's term, shown to the user.
    std::string_view name;
    // Decoded offset in the Performance (Slot A's byte for per-Slot settings).
    std::size_t offset;
    bool perSlot;
    PerformanceSettingBits bits;
    // The documented range. Values read from the synth may lie outside it and are kept as they
    // are (research §3: offset 312 reaches 14 in [OFF-FB]).
    int minValue;
    int maxValue;
    PerformanceSettingAccess access;
    PerformanceSettingDisplay display;
    // Named steps, lowest first, where the manual gives them; empty otherwise.
    std::span<const std::string_view> choices;
    // A name for the lowest value when it means something else ("Any note", "Off"); empty if none.
    std::string_view lowestValueName;
};

std::span<const PerformanceSettingInfo> performanceSettings();
const PerformanceSettingInfo& performanceSettingInfo(PerformanceSetting setting);

// The decoded Performance offset of `setting` for `slot` (the Slot is ignored for settings that
// are not per Slot).
std::size_t performanceSettingOffset(PerformanceSetting setting, Slot slot);

// DAW-facing identity of a Performance setting: "perf_splitPoint", "perf_layerA"; and the name
// the DAW shows, "Performance: Split Point", "Performance: Slot A Layer".
std::string performanceDawParameterId(PerformanceSetting setting, Slot slot);
std::string performanceDawParameterName(PerformanceSetting setting, Slot slot);

} // namespace nl2x
