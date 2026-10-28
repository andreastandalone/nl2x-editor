#include "nl2x/ProgramParameters.h"

#include <array>
#include <cassert>

namespace nl2x {

namespace {

using P = ProgramParameter;

// Base parameters 0–24 have their Morph Amount 25 offsets later [OFF-2X p.111].
constexpr std::uint8_t kFirstMorphAmountOffset = 25;
constexpr std::uint8_t kMorphAmountCount = 25;

constexpr std::uint8_t offsetOf(P parameter)
{
    return static_cast<std::uint8_t>(parameter);
}

// A parameter velocity or the mod wheel can morph (offsets 0–24).
constexpr ProgramParameterInfo morphable(P id, std::string_view key, std::string_view name, int minValue, int maxValue)
{
    return { id, key, name, offsetOf(id), minValue, maxValue,
             static_cast<P>(offsetOf(id) + kFirstMorphAmountOffset), std::nullopt };
}

// A Morph Amount (offsets 25–49): signed, 0 = off [OFF-2X p.111].
constexpr ProgramParameterInfo morphAmount(P id, std::string_view key, std::string_view name)
{
    return { id, key, name, offsetOf(id), -128, 127, std::nullopt,
             static_cast<P>(offsetOf(id) - kFirstMorphAmountOffset) };
}

// Any other parameter (offsets 50–65): stepped buttons and switches.
constexpr ProgramParameterInfo plain(P id, std::string_view key, std::string_view name, int minValue, int maxValue)
{
    return { id, key, name, offsetOf(id), minValue, maxValue, std::nullopt, std::nullopt };
}

// Names follow the manual's panel and CC chart terms [OFF-2X p.103–104, 111–112]; ranges are the
// documented ones (research §3).
constexpr std::array<ProgramParameterInfo, kProgramParameterCount> kProgramParameters {
    morphable(P::Osc2Semitones, "osc2Semitones", "Osc2 Semitones", 0, 120),
    morphable(P::Osc2FineTune, "osc2FineTune", "Osc2 Fine Tune", 0, 127),
    morphable(P::OscMix, "oscMix", "Osc Mix", 0, 127),
    morphable(P::FilterCutoff, "filterCutoff", "Filter Cutoff", 0, 127),
    morphable(P::FilterResonance, "filterResonance", "Filter Resonance", 0, 127),
    morphable(P::FilterEnvAmount, "filterEnvAmount", "Filter Env Amount", 0, 127),
    morphable(P::PulseWidth, "pulseWidth", "Osc Pulse Width", 0, 127),
    morphable(P::FmDepth, "fmDepth", "Osc FM Depth", 0, 127),
    morphable(P::FilterEnvAttack, "filterEnvAttack", "Filter Env Attack", 0, 127),
    morphable(P::FilterEnvDecay, "filterEnvDecay", "Filter Env Decay", 0, 127),
    morphable(P::FilterEnvSustain, "filterEnvSustain", "Filter Env Sustain", 0, 127),
    morphable(P::FilterEnvRelease, "filterEnvRelease", "Filter Env Release", 0, 127),
    morphable(P::AmpEnvAttack, "ampEnvAttack", "Amp Env Attack", 0, 127),
    morphable(P::AmpEnvDecay, "ampEnvDecay", "Amp Env Decay", 0, 127),
    morphable(P::AmpEnvSustain, "ampEnvSustain", "Amp Env Sustain", 0, 127),
    morphable(P::AmpEnvRelease, "ampEnvRelease", "Amp Env Release", 0, 127),
    morphable(P::PortamentoTime, "portamentoTime", "Portamento Time", 0, 127),
    morphable(P::Gain, "gain", "Gain", 0, 127),
    morphable(P::ModEnvAttack, "modEnvAttack", "Mod Env Attack", 0, 127),
    morphable(P::ModEnvDecay, "modEnvDecay", "Mod Env Decay", 0, 127),
    // 64 = centre.
    morphable(P::ModEnvAmount, "modEnvAmount", "Mod Env Amount", 0, 127),
    morphable(P::Lfo1Rate, "lfo1Rate", "LFO1 Rate", 0, 127),
    morphable(P::Lfo1Amount, "lfo1Amount", "LFO1 Amount", 0, 127),
    morphable(P::Lfo2Rate, "lfo2Rate", "LFO2/Arp Rate", 0, 127),
    morphable(P::Lfo2Amount, "lfo2Amount", "LFO2 Amount/Arp Range", 0, 127),

    morphAmount(P::Osc2SemitonesMorphAmount, "osc2SemitonesMorphAmount", "Osc2 Semitones Morph Amount"),
    morphAmount(P::Osc2FineTuneMorphAmount, "osc2FineTuneMorphAmount", "Osc2 Fine Tune Morph Amount"),
    morphAmount(P::OscMixMorphAmount, "oscMixMorphAmount", "Osc Mix Morph Amount"),
    morphAmount(P::FilterCutoffMorphAmount, "filterCutoffMorphAmount", "Filter Cutoff Morph Amount"),
    morphAmount(P::FilterResonanceMorphAmount, "filterResonanceMorphAmount", "Filter Resonance Morph Amount"),
    morphAmount(P::FilterEnvAmountMorphAmount, "filterEnvAmountMorphAmount", "Filter Env Amount Morph Amount"),
    morphAmount(P::PulseWidthMorphAmount, "pulseWidthMorphAmount", "Osc Pulse Width Morph Amount"),
    morphAmount(P::FmDepthMorphAmount, "fmDepthMorphAmount", "Osc FM Depth Morph Amount"),
    morphAmount(P::FilterEnvAttackMorphAmount, "filterEnvAttackMorphAmount", "Filter Env Attack Morph Amount"),
    morphAmount(P::FilterEnvDecayMorphAmount, "filterEnvDecayMorphAmount", "Filter Env Decay Morph Amount"),
    morphAmount(P::FilterEnvSustainMorphAmount, "filterEnvSustainMorphAmount", "Filter Env Sustain Morph Amount"),
    morphAmount(P::FilterEnvReleaseMorphAmount, "filterEnvReleaseMorphAmount", "Filter Env Release Morph Amount"),
    morphAmount(P::AmpEnvAttackMorphAmount, "ampEnvAttackMorphAmount", "Amp Env Attack Morph Amount"),
    morphAmount(P::AmpEnvDecayMorphAmount, "ampEnvDecayMorphAmount", "Amp Env Decay Morph Amount"),
    morphAmount(P::AmpEnvSustainMorphAmount, "ampEnvSustainMorphAmount", "Amp Env Sustain Morph Amount"),
    morphAmount(P::AmpEnvReleaseMorphAmount, "ampEnvReleaseMorphAmount", "Amp Env Release Morph Amount"),
    morphAmount(P::PortamentoTimeMorphAmount, "portamentoTimeMorphAmount", "Portamento Time Morph Amount"),
    morphAmount(P::GainMorphAmount, "gainMorphAmount", "Gain Morph Amount"),
    morphAmount(P::ModEnvAttackMorphAmount, "modEnvAttackMorphAmount", "Mod Env Attack Morph Amount"),
    morphAmount(P::ModEnvDecayMorphAmount, "modEnvDecayMorphAmount", "Mod Env Decay Morph Amount"),
    morphAmount(P::ModEnvAmountMorphAmount, "modEnvAmountMorphAmount", "Mod Env Amount Morph Amount"),
    morphAmount(P::Lfo1RateMorphAmount, "lfo1RateMorphAmount", "LFO1 Rate Morph Amount"),
    morphAmount(P::Lfo1AmountMorphAmount, "lfo1AmountMorphAmount", "LFO1 Amount Morph Amount"),
    morphAmount(P::Lfo2RateMorphAmount, "lfo2RateMorphAmount", "LFO2/Arp Rate Morph Amount"),
    morphAmount(P::Lfo2AmountMorphAmount, "lfo2AmountMorphAmount", "LFO2 Amount/Arp Range Morph Amount"),

    // 3 = sine.
    plain(P::Osc1Waveform, "osc1Waveform", "Osc1 Waveform", 0, 3),
    // 3 = noise.
    plain(P::Osc2Waveform, "osc2Waveform", "Osc2 Waveform", 0, 3),
    // Bits 0, 1 and 4: the largest documented combination is 19.
    plain(P::SyncRingModDistortion, "syncRingModDistortion", "Sync/Ring Mod/Distortion", 0, 19),
    plain(P::FilterType, "filterType", "Filter Type", 0, 4),
    plain(P::Osc2KeyboardTrack, "osc2KeyboardTrack", "Osc2 Keyboard Tracking", 0, 1),
    plain(P::FilterKeyboardTrack, "filterKeyboardTrack", "Filter Keyboard Track", 0, 3),
    plain(P::Lfo1Waveform, "lfo1Waveform", "LFO1 Waveform", 0, 4),
    plain(P::Lfo1Destination, "lfo1Destination", "LFO1 Destination", 0, 4),
    plain(P::VoiceMode, "voiceMode", "Poly/Legato/Mono", 0, 2),
    plain(P::ModWheelDestination, "modWheelDestination", "Mod Wheel Destination", 0, 4),
    plain(P::Unison, "unison", "Unison", 0, 1),
    plain(P::ModEnvDestination, "modEnvDestination", "Mod Env Destination", 0, 3),
    plain(P::PortamentoAuto, "portamentoAuto", "Portamento Auto", 0, 1),
    plain(P::FilterVelocity, "filterVelocity", "Filter Velocity", 0, 1),
    plain(P::OctaveShift, "octaveShift", "Oct Shift", 0, 4),
    // 8 = off.
    plain(P::Lfo2DestinationArpMode, "lfo2DestinationArpMode", "LFO2 Dest/Arp Mode", 0, 8),
};

constexpr bool tableIsInOffsetOrder()
{
    for (std::size_t i = 0; i < kProgramParameters.size(); ++i)
        if (static_cast<std::size_t>(kProgramParameters[i].id) != i || kProgramParameters[i].sysExOffset != i)
            return false;
    return true;
}
static_assert(tableIsInOffsetOrder(), "kProgramParameters must hold one entry per offset, in ProgramParameter order");
static_assert(kFirstMorphAmountOffset + kMorphAmountCount == offsetOf(P::Osc1Waveform));

} // namespace

std::span<const ProgramParameterInfo> programParameters()
{
    return kProgramParameters;
}

const ProgramParameterInfo& programParameterInfo(ProgramParameter parameter)
{
    const auto& info = kProgramParameters[static_cast<std::size_t>(parameter)];
    assert(info.id == parameter);
    return info;
}

std::string dawParameterId(Slot slot, ProgramParameter parameter)
{
    return std::string(1, slotLetter(slot)) + "_" + std::string(programParameterInfo(parameter).key);
}

std::string dawParameterName(Slot slot, ProgramParameter parameter)
{
    return std::string(1, slotLetter(slot)) + ": " + std::string(programParameterInfo(parameter).name);
}

} // namespace nl2x
