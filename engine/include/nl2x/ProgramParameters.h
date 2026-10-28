#pragma once

#include "nl2x/Slot.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace nl2x {

// A Program holds 66 parameters [OFF-2X p.111–112].
inline constexpr std::size_t kProgramParameterCount = 66;

// Every Program parameter, one per decoded offset of the Program dump, in offset order
// (research §3, patch layout). The numeric value of an enumerator is its offset; DAW automation
// and saved projects refer to ProgramParameterInfo::key instead, so the order may never be used
// as an identity outside the engine.
enum class ProgramParameter : std::uint8_t {
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
    // The 25 Morph Amounts, offsets 25–49, in the same order as their base parameters 0–24.
    Osc2SemitonesMorphAmount,
    Osc2FineTuneMorphAmount,
    OscMixMorphAmount,
    FilterCutoffMorphAmount,
    FilterResonanceMorphAmount,
    FilterEnvAmountMorphAmount,
    PulseWidthMorphAmount,
    FmDepthMorphAmount,
    FilterEnvAttackMorphAmount,
    FilterEnvDecayMorphAmount,
    FilterEnvSustainMorphAmount,
    FilterEnvReleaseMorphAmount,
    AmpEnvAttackMorphAmount,
    AmpEnvDecayMorphAmount,
    AmpEnvSustainMorphAmount,
    AmpEnvReleaseMorphAmount,
    PortamentoTimeMorphAmount,
    GainMorphAmount,
    ModEnvAttackMorphAmount,
    ModEnvDecayMorphAmount,
    ModEnvAmountMorphAmount,
    Lfo1RateMorphAmount,
    Lfo1AmountMorphAmount,
    Lfo2RateMorphAmount,
    Lfo2AmountMorphAmount,
    Osc1Waveform,
    Osc2Waveform,
    // One byte for the Ring Mod/Sync button and Filter Distortion: bit 0 = sync, bit 1 = ring
    // mod, bit 4 = distortion [OFF-2X p.111]. Kept as one raw value until #6 maps CC35 and CC80.
    SyncRingModDistortion,
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

struct ProgramParameterInfo {
    ProgramParameter id;
    // Never changes once released: DAW automation and saved projects refer to it.
    std::string_view key;
    // The manual's term, shown to the user.
    std::string_view name;
    // Panel CC [OFF-2X p.103–104], or none for parameters reachable only by SysEx. Only the
    // parameters exposed so far carry one; #6 fills in the rest from research §2.
    std::optional<std::uint8_t> cc;
    // Position in the decoded Program dump [OFF-2X p.111–112]. Pairing it with the CC is [INF]
    // (research §2): the names match one to one.
    std::uint8_t sysExOffset;
    // The documented range. Values read from the synth or from files may lie outside it and are
    // kept as they are (research §3).
    int minValue;
    int maxValue;
    // Named steps for stepped parameters, lowest first; empty for continuous ones and for stepped
    // ones whose order is not yet known (research §2, plan step 9).
    std::span<const std::string_view> choices;
    // On a base parameter that velocity or the mod wheel can morph: its Morph Amount.
    std::optional<ProgramParameter> morphAmount;
    // On a Morph Amount: the parameter it morphs.
    std::optional<ProgramParameter> morphedParameter;
};

std::span<const ProgramParameterInfo> programParameters();
const ProgramParameterInfo& programParameterInfo(ProgramParameter parameter);

// DAW-facing identity of a Slot's parameter: a stable ID ("A_filterCutoff") and the name the
// DAW shows ("A: Filter Cutoff").
std::string dawParameterId(Slot slot, ProgramParameter parameter);
std::string dawParameterName(Slot slot, ProgramParameter parameter);

} // namespace nl2x
