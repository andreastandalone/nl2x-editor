#include "nl2x/CcParameters.h"

#include "nl2x/HardwareAssumptions.h"

#include <array>
#include <bit>
#include <cassert>

namespace nl2x {

namespace {

using C = CcParameter;
using P = ProgramParameter;
using Kind = CcParameterKind;

constexpr std::uint8_t kWholeByte = 0xFF;

constexpr CcParameterInfo knob(C id, std::string_view key, std::string_view name, std::uint8_t cc, P byte,
                               int maxValue = 127)
{
    return { id, key, name, cc, byte, kWholeByte, Kind::Knob, 0, maxValue, {} };
}

constexpr CcParameterInfo stepped(C id, std::string_view key, std::string_view name, std::uint8_t cc, P byte,
                                  std::span<const std::string_view> choices, std::uint8_t bits = kWholeByte)
{
    return { id, key, name, cc, byte, bits, Kind::Stepped, 0, static_cast<int>(choices.size()) - 1, choices };
}

constexpr CcParameterInfo onOff(C id, std::string_view key, std::string_view name, std::uint8_t cc, P byte,
                                std::uint8_t bits = kWholeByte)
{
    return { id, key, name, cc, byte, bits, Kind::Switch, 0, 1, {} };
}

// CC numbers from the manual's CC chart [OFF-2X p.103–104]; names and ranges as in the Program
// parameter table (research §2–3). The order of the named steps is HardwareAssumptions.h's.
constexpr std::array<CcParameterInfo, kCcParameterCount> kCcParameters {
    knob(C::Osc2Semitones, "osc2Semitones", "Osc2 Semitones", 78, P::Osc2Semitones, 120),
    knob(C::Osc2FineTune, "osc2FineTune", "Osc2 Fine Tune", 33, P::Osc2FineTune),
    knob(C::OscMix, "oscMix", "Osc Mix", 8, P::OscMix),
    knob(C::FilterCutoff, "filterCutoff", "Filter Cutoff", 74, P::FilterCutoff),
    knob(C::FilterResonance, "filterResonance", "Filter Resonance", 42, P::FilterResonance),
    knob(C::FilterEnvAmount, "filterEnvAmount", "Filter Env Amount", 43, P::FilterEnvAmount),
    knob(C::PulseWidth, "pulseWidth", "Osc Pulse Width", 79, P::PulseWidth),
    knob(C::FmDepth, "fmDepth", "Osc FM Depth", 70, P::FmDepth),
    knob(C::FilterEnvAttack, "filterEnvAttack", "Filter Env Attack", 38, P::FilterEnvAttack),
    knob(C::FilterEnvDecay, "filterEnvDecay", "Filter Env Decay", 39, P::FilterEnvDecay),
    knob(C::FilterEnvSustain, "filterEnvSustain", "Filter Env Sustain", 40, P::FilterEnvSustain),
    knob(C::FilterEnvRelease, "filterEnvRelease", "Filter Env Release", 41, P::FilterEnvRelease),
    knob(C::AmpEnvAttack, "ampEnvAttack", "Amp Env Attack", 73, P::AmpEnvAttack),
    knob(C::AmpEnvDecay, "ampEnvDecay", "Amp Env Decay", 36, P::AmpEnvDecay),
    knob(C::AmpEnvSustain, "ampEnvSustain", "Amp Env Sustain", 37, P::AmpEnvSustain),
    knob(C::AmpEnvRelease, "ampEnvRelease", "Amp Env Release", 72, P::AmpEnvRelease),
    knob(C::PortamentoTime, "portamentoTime", "Portamento Time", 5, P::PortamentoTime),
    knob(C::Gain, "gain", "Gain", 7, P::Gain),
    knob(C::ModEnvAttack, "modEnvAttack", "Mod Env Attack", 26, P::ModEnvAttack),
    knob(C::ModEnvDecay, "modEnvDecay", "Mod Env Decay", 27, P::ModEnvDecay),
    knob(C::ModEnvAmount, "modEnvAmount", "Mod Env Amount", 29, P::ModEnvAmount),
    knob(C::Lfo1Rate, "lfo1Rate", "LFO1 Rate", 19, P::Lfo1Rate),
    knob(C::Lfo1Amount, "lfo1Amount", "LFO1 Amount", 22, P::Lfo1Amount),
    knob(C::Lfo2Rate, "lfo2Rate", "LFO2/Arp Rate", 23, P::Lfo2Rate),
    knob(C::Lfo2Amount, "lfo2Amount", "LFO2 Amount/Arp Range", 25, P::Lfo2Amount),
    stepped(C::Osc1Waveform, "osc1Waveform", "Osc1 Waveform", 30, P::Osc1Waveform, hardware::kOsc1WaveformChoices),
    stepped(C::Osc2Waveform, "osc2Waveform", "Osc2 Waveform", 31, P::Osc2Waveform, hardware::kOsc2WaveformChoices),
    stepped(C::RingModSync, "ringModSync", "Ring Mod/Sync", hardware::kRingModSyncCc, P::SyncRingModDistortion,
            hardware::kRingModSyncChoices, hardware::kRingModSyncBits),
    onOff(C::FilterDistortion, "filterDistortion", "Filter Distortion", 80, P::SyncRingModDistortion,
          hardware::kFilterDistortionBit),
    stepped(C::FilterType, "filterType", "Filter Type", 44, P::FilterType, hardware::kFilterTypeChoices),
    onOff(C::Osc2KeyboardTrack, "osc2KeyboardTrack", "Osc2 Keyboard Tracking", 34, P::Osc2KeyboardTrack),
    stepped(C::FilterKeyboardTrack, "filterKeyboardTrack", "Filter Keyboard Track", 46, P::FilterKeyboardTrack,
            hardware::kFilterKeyboardTrackChoices),
    stepped(C::Lfo1Waveform, "lfo1Waveform", "LFO1 Waveform", 20, P::Lfo1Waveform, hardware::kLfo1WaveformChoices),
    stepped(C::Lfo1Destination, "lfo1Destination", "LFO1 Destination", 21, P::Lfo1Destination,
            hardware::kLfo1DestinationChoices),
    stepped(C::VoiceMode, "voiceMode", "Poly/Legato/Mono", 15, P::VoiceMode, hardware::kVoiceModeChoices),
    stepped(C::ModWheelDestination, "modWheelDestination", "Mod Wheel Destination", 18, P::ModWheelDestination,
            hardware::kModWheelDestinationChoices),
    onOff(C::Unison, "unison", "Unison", 16, P::Unison),
    stepped(C::ModEnvDestination, "modEnvDestination", "Mod Env Destination", 28, P::ModEnvDestination,
            hardware::kModEnvDestinationChoices),
    onOff(C::PortamentoAuto, "portamentoAuto", "Portamento Auto", 65, P::PortamentoAuto),
    onOff(C::FilterVelocity, "filterVelocity", "Filter Velocity", 45, P::FilterVelocity),
    stepped(C::OctaveShift, "octaveShift", "Oct Shift", 17, P::OctaveShift, hardware::kOctaveShiftChoices),
    stepped(C::Lfo2DestinationArpMode, "lfo2DestinationArpMode", "LFO2 Dest/Arp Mode", 24, P::Lfo2DestinationArpMode,
            hardware::kLfo2DestinationArpModeChoices),
};

constexpr bool tableIsInEnumOrder()
{
    for (std::size_t i = 0; i < kCcParameters.size(); ++i)
        if (static_cast<std::size_t>(kCcParameters[i].id) != i)
            return false;
    return true;
}
static_assert(tableIsInEnumOrder(), "kCcParameters must hold one entry per CcParameter, in enum order");

constexpr bool ccNumbersAreUnique()
{
    for (std::size_t i = 0; i < kCcParameters.size(); ++i)
        for (std::size_t j = i + 1; j < kCcParameters.size(); ++j)
            if (kCcParameters[i].cc == kCcParameters[j].cc)
                return false;
    return true;
}
static_assert(ccNumbersAreUnique());

} // namespace

std::span<const CcParameterInfo> ccParameters()
{
    return kCcParameters;
}

const CcParameterInfo& ccParameterInfo(CcParameter parameter)
{
    const auto& info = kCcParameters[static_cast<std::size_t>(parameter)];
    assert(info.id == parameter);
    return info;
}

std::optional<CcParameter> ccParameterOfController(std::uint8_t cc)
{
    for (const auto& info : kCcParameters)
        if (info.cc == cc)
            return info.id;
    return std::nullopt;
}

int ccParameterValue(const Program& program, CcParameter parameter)
{
    const auto& info = ccParameterInfo(parameter);
    const auto value = program.value(info.byte);
    if (info.bits == kWholeByte)
        return value;
    return (static_cast<std::uint8_t>(value) & info.bits) >> std::countr_zero(info.bits);
}

void setCcParameterValue(Program& program, CcParameter parameter, int value)
{
    const auto& info = ccParameterInfo(parameter);
    if (info.bits == kWholeByte) {
        program.setValue(info.byte, value);
        return;
    }
    const auto field = static_cast<std::uint8_t>((value << std::countr_zero(info.bits)) & info.bits);
    const auto kept = static_cast<std::uint8_t>(static_cast<std::uint8_t>(program.value(info.byte)) & ~info.bits);
    program.setValue(info.byte, static_cast<std::int8_t>(static_cast<std::uint8_t>(kept | field)));
}

std::uint8_t ccDataOf(CcParameter parameter, int value)
{
    const auto& info = ccParameterInfo(parameter);
    return hardware::ccDataFromValue(info.cc, info.kind == CcParameterKind::Switch, value);
}

std::optional<int> valueOfCcData(CcParameter parameter, std::uint8_t data)
{
    const auto& info = ccParameterInfo(parameter);
    return hardware::valueFromCcData(info.cc, info.kind == CcParameterKind::Switch, data);
}

std::string dawParameterId(Slot slot, CcParameter parameter)
{
    return std::string(1, slotLetter(slot)) + "_" + std::string(ccParameterInfo(parameter).key);
}

std::string dawParameterName(Slot slot, CcParameter parameter)
{
    return std::string(1, slotLetter(slot)) + ": " + std::string(ccParameterInfo(parameter).name);
}

} // namespace nl2x
