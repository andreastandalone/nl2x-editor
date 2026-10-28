#pragma once

// The Program parameters the synth's panel sends and receives as CCs [OFF-2X p.103–104]
// (research §2): what the editor edits with a CC and exposes as DAW parameters. Each one is a
// field of the Program dump: a whole byte, or some bits of byte 52, which holds the RING MOD/SYNC
// button and Filter Distortion together [OFF-2X p.111]. Parameters without a CC (the Morph
// Amounts) are not here; they travel in Program dumps (#7).

#include "nl2x/Program.h"
#include "nl2x/ProgramParameters.h"
#include "nl2x/Slot.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace nl2x {

// In the order of the Program dump. Identity outside the engine is CcParameterInfo::key.
enum class CcParameter : std::uint8_t {
    Osc2Semitones,
    Osc2FineTune,
    OscMix,
    FilterCutoff,
    FilterResonance,
    FilterEnvAmount,
    PulseWidth,
    FmDepth,
    FilterEnvAttack,
    FilterEnvDecay,
    FilterEnvSustain,
    FilterEnvRelease,
    AmpEnvAttack,
    AmpEnvDecay,
    AmpEnvSustain,
    AmpEnvRelease,
    PortamentoTime,
    Gain,
    ModEnvAttack,
    ModEnvDecay,
    ModEnvAmount,
    Lfo1Rate,
    Lfo1Amount,
    Lfo2Rate,
    Lfo2Amount,
    Osc1Waveform,
    Osc2Waveform,
    // Byte 52, bits 0–1.
    RingModSync,
    // Byte 52, bit 4.
    FilterDistortion,
    FilterType,
    Osc2KeyboardTrack,
    FilterKeyboardTrack,
    Lfo1Waveform,
    Lfo1Destination,
    VoiceMode,
    ModWheelDestination,
    Unison,
    ModEnvDestination,
    PortamentoAuto,
    FilterVelocity,
    OctaveShift,
    Lfo2DestinationArpMode,
};

inline constexpr std::size_t kCcParameterCount = 42;

// How the user sets it on the panel, and so how the editor and the DAW show it.
enum class CcParameterKind : std::uint8_t {
    // A knob: a number in minValue … maxValue.
    Knob,
    // A button stepping through named settings (`choices`, lowest first).
    Stepped,
    // An on/off button: 0 = off, 1 = on.
    Switch,
};

struct CcParameterInfo {
    CcParameter id;
    // Never changes once released: DAW automation and saved projects refer to it. For a whole
    // byte it is the ProgramParameter's key, so "A_filterCutoff" stays what it was.
    std::string_view key;
    // The manual's term, shown to the user.
    std::string_view name;
    // Panel CC [OFF-2X p.103–104].
    std::uint8_t cc;
    // The byte of the Program dump that holds it [OFF-2X p.111–112].
    ProgramParameter byte;
    // The bits of that byte it uses; 0xFF = the whole byte, read as a signed value.
    std::uint8_t bits;
    CcParameterKind kind;
    // The documented range. Values read from the synth may lie outside it and are kept.
    int minValue;
    int maxValue;
    // For Stepped parameters: one name per value from minValue up (HardwareAssumptions.h).
    std::span<const std::string_view> choices;
};

std::span<const CcParameterInfo> ccParameters();
const CcParameterInfo& ccParameterInfo(CcParameter parameter);
// The parameter a panel CC controls; empty for any other controller (mod wheel, pedals, Bank
// Select, unknown numbers).
std::optional<CcParameter> ccParameterOfController(std::uint8_t cc);

// The parameter's value in a Program, exactly as stored: a whole byte may hold a value outside
// the documented range; a bit field is read without the other bits of its byte.
int ccParameterValue(const Program& program, CcParameter parameter);
// Sets the parameter in `program`, keeping the other bits of a shared byte. `value` is within
// −128 … 127 for a whole byte; a bit field keeps only the bits it has.
void setCcParameterValue(Program& program, CcParameter parameter, int value);

// The CC value that carries `value`, and the value a received CC value stands for (empty when
// it cannot be stored). The rules are HardwareAssumptions.h's.
std::uint8_t ccDataOf(CcParameter parameter, int value);
std::optional<int> valueOfCcData(CcParameter parameter, std::uint8_t data);

// DAW-facing identity of a Slot's parameter: a stable ID ("A_filterCutoff") and the name the
// DAW shows ("A: Filter Cutoff").
std::string dawParameterId(Slot slot, CcParameter parameter);
std::string dawParameterName(Slot slot, CcParameter parameter);

} // namespace nl2x
