#include "nl2x/PerformanceSettings.h"

#include "nl2x/SysEx.h"

#include <array>
#include <cassert>

namespace nl2x {

namespace {

using S = PerformanceSetting;
using Bits = PerformanceSettingBits;
using Access = PerformanceSettingAccess;
using Display = PerformanceSettingDisplay;

constexpr std::array<std::string_view, 2> kOffOn { "Off", "On" };

// Special functions 1 and 2: the restart interval, "1.oF" … "1. 7" [OFF-2X p.68–69].
constexpr std::array<std::string_view, 8> kSyncIntervals {
    "Off", "Every other bar", "Whole note", "Half note", "Quarter note", "Eighth note", "Eighth note triplet",
    "Sixteenth note",
};

constexpr std::array<std::string_view, 4> kSlotLetters { "A", "B", "C", "D" };

// Aftertouch and Control Pedal destinations, in the order the manual lists them [OFF-2X p.19,
// 75]; that this order is the stored value 0–4 is [INF].
constexpr std::array<std::string_view, 5> kModulationDestinations {
    "LFO 1 Amount", "LFO 2 Amount", "Filter Cutoff Frequency", "FM Amount", "Oscillator 2 Pitch",
};

constexpr PerformanceSettingInfo perSlot(S id, std::string_view key, std::string_view name, std::size_t offset,
                                         int minValue, int maxValue, Access access = Access::Editable,
                                         Display display = Display::Number,
                                         std::span<const std::string_view> choices = {},
                                         std::string_view lowestValueName = {})
{
    return { id, key, name, offset, true, Bits::Byte, minValue, maxValue, access, display, choices, lowestValueName };
}

constexpr PerformanceSettingInfo single(S id, std::string_view key, std::string_view name, std::size_t offset,
                                        int minValue, int maxValue, Access access = Access::Editable,
                                        std::span<const std::string_view> choices = {},
                                        Bits bits = Bits::Byte)
{
    return { id, key, name, offset, false, bits, minValue, maxValue, access, Display::Number, choices, {} };
}

// Offsets and ranges from research §3 [OFF-2X p.112]; "not received" as printed there.
constexpr std::array kPerformanceSettings {
    // −1 is assumed to mean "off" (HardwareAssumptions.h, slotChannelFromPerformance).
    perSlot(S::SlotChannel, "slotChannel", "MIDI Channel", 264, 0, 15, Access::Editable, Display::MidiChannel),
    perSlot(S::Lfo1Sync, "lfo1Sync", "LFO 1 Sync", 268, 0, 7, Access::Editable, Display::Number, kSyncIntervals),
    perSlot(S::Lfo2Sync, "lfo2Sync", "LFO 2/Arp Sync", 272, 0, 7, Access::Editable, Display::Number, kSyncIntervals),
    perSlot(S::FilterEnvTrig, "filterEnvTrig", "Filter Env Trig", 276, 0, 1, Access::Editable, Display::Number, kOffOn),
    perSlot(S::FilterEnvTrigChannel, "filterEnvTrigChannel", "Filter Env Trig MIDI Channel", 280, 0, 15,
            Access::Editable, Display::MidiChannel),
    // 23 = any note [OFF-2X p.70; research §3].
    perSlot(S::FilterEnvTrigNote, "filterEnvTrigNote", "Filter Env Trig Note", 284, 23, 127, Access::Editable,
            Display::Note, {}, "Any note"),
    perSlot(S::AmpEnvTrig, "ampEnvTrig", "Amp Env Trig", 288, 0, 1, Access::Editable, Display::Number, kOffOn),
    perSlot(S::AmpEnvTrigChannel, "ampEnvTrigChannel", "Amp Env Trig MIDI Channel", 292, 0, 15, Access::Editable,
            Display::MidiChannel),
    perSlot(S::AmpEnvTrigNote, "ampEnvTrigNote", "Amp Env Trig Note", 296, 23, 127, Access::Editable, Display::Note,
            {}, "Any note"),
    perSlot(S::ExternalVelocityMorph, "externalVelocityMorph", "External Velocity Morph", 300, 0, 1, Access::Editable,
            Display::Number, kOffOn),
    perSlot(S::ExternalVelocityMorphChannel, "externalVelocityMorphChannel", "External Velocity Morph MIDI Channel",
            304, 0, 15, Access::Editable, Display::MidiChannel),
    perSlot(S::ExternalVelocityMorphNote, "externalVelocityMorphNote", "External Velocity Morph Note", 308, 23, 127,
            Access::Editable, Display::Note, {}, "Any note"),
    single(S::PitchBendRange, "pitchBendRange", "Pitch Bend Range", 312, 0, 8),
    single(S::UnisonDetune, "unisonDetune", "Unison Detune", 313, 0, 8),
    single(S::OutModeAB, "outModeAB", "Out Mode for Slots A and B", 314, 0, 3, Access::Editable, {}, Bits::LowNibble),
    single(S::OutModeCD, "outModeCD", "Out Mode for Slots C and D", 314, 0, 3, Access::Editable, {}, Bits::HighNibble),
    single(S::GlobalChannel, "globalChannel", "Global MIDI Channel", 315, 0, 15, Access::NotReceived),
    single(S::ProgramChange, "programChange", "Prog. Change On/Off", 316, 0, 1, Access::NotReceived, kOffOn),
    single(S::ControlChange, "controlChange", "Controllers On/Off", 317, 0, 1, Access::NotReceived, kOffOn),
    single(S::MasterTune, "masterTune", "Master Tune", 318, -99, 99, Access::NotReceived),
    single(S::PedalType, "pedalType", "Sustain Pedal Polarity", 319, 0, 2, Access::NotReceived),
    single(S::LocalControl, "localControl", "Local On/Off", 320, 0, 1, Access::NotReceived, kOffOn),
    single(S::KeyboardOctaveShift, "keyboardOctaveShift", "Keyboard Octave Shift", 321, 0, 4, Access::NotReceived),
    single(S::SelectedSlot, "selectedSlot", "Selected Slot", 322, 0, 3, Access::Editable, kSlotLetters),
    single(S::ArpMidiOut, "arpMidiOut", "Arp. MIDI Out On/Off", 323, 0, 1, Access::NotReceived, kOffOn),
    perSlot(S::Layer, "layer", "Layer", 324, 0, 1, Access::Editable, Display::Number, kOffOn),
    perSlot(S::ProgramNumber, "programNumber", "Program", 328, 0, 98, Access::ReadOnly),
    perSlot(S::ProgramBank, "programBank", "Program Bank", 332, 0, 3, Access::ReadOnly),
    perSlot(S::AftertouchAmount, "aftertouchAmount", "Aftertouch Amount", 336, 0, 7, Access::Editable,
            Display::Number, {}, "Off"),
    perSlot(S::AftertouchDestination, "aftertouchDestination", "Aftertouch Assignment", 340, 0, 4, Access::Editable,
            Display::Number, kModulationDestinations),
    perSlot(S::ControlPedalAmount, "controlPedalAmount", "Control Pedal Amount", 344, 0, 7),
    perSlot(S::ControlPedalDestination, "controlPedalDestination", "Control Pedal Assignment", 348, 0, 4,
            Access::Editable, Display::Number, kModulationDestinations),
    single(S::KeyboardSplit, "split", "Keyboard Split", 352, 0, 1, Access::Editable, kOffOn),
    single(S::SplitPoint, "splitPoint", "Split Point", 353, 0, 127),
};

constexpr bool tableIsConsistent()
{
    for (std::size_t i = 0; i < kPerformanceSettings.size(); ++i) {
        const auto& info = kPerformanceSettings[i];
        if (static_cast<std::size_t>(info.id) != i || info.offset < kProgramParameterCount * 4
            || info.offset + (info.perSlot ? 3 : 0) >= kPerformanceParameterCount)
            return false;
        if (! info.choices.empty() && info.choices.size() != static_cast<std::size_t>(info.maxValue - info.minValue + 1))
            return false;
    }
    return true;
}
static_assert(tableIsConsistent(), "kPerformanceSettings must be in PerformanceSetting order, inside the settings");

} // namespace

std::span<const PerformanceSettingInfo> performanceSettings()
{
    return kPerformanceSettings;
}

const PerformanceSettingInfo& performanceSettingInfo(PerformanceSetting setting)
{
    const auto& info = kPerformanceSettings[static_cast<std::size_t>(setting)];
    assert(info.id == setting);
    return info;
}

std::size_t performanceSettingOffset(PerformanceSetting setting, Slot slot)
{
    const auto& info = performanceSettingInfo(setting);
    return info.offset + (info.perSlot ? slotIndex(slot) : 0);
}

std::string performanceDawParameterId(PerformanceSetting setting, Slot slot)
{
    const auto& info = performanceSettingInfo(setting);
    auto id = "perf_" + std::string(info.key);
    if (info.perSlot)
        id += slotLetter(slot);
    return id;
}

std::string performanceDawParameterName(PerformanceSetting setting, Slot slot)
{
    const auto& info = performanceSettingInfo(setting);
    std::string name = "Performance: ";
    if (info.perSlot)
        name += std::string("Slot ") + slotLetter(slot) + " ";
    return name + std::string(info.name);
}

} // namespace nl2x
