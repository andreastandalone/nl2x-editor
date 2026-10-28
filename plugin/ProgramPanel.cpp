#include "ProgramPanel.h"

#include "NordLookAndFeel.h"

#include "nl2x/MorphAmounts.h"

namespace {

using C = nl2x::CcParameter;

constexpr int kSlotButtonsGroup = 4401;
constexpr int kGap = NordMetrics::gap;
// Where controls start below a section's title bar.
constexpr int kTop = NordMetrics::sectionTitleHeight + 6;

// The three columns of the upper panel and the height of the upper part.
constexpr int kLeftWidth = 330;
constexpr int kCentreWidth = 330;
constexpr int kRightWidth = ProgramPanel::kWidth - 4 * kGap - kLeftWidth - kCentreWidth;
constexpr int kUpperHeight = 406;
constexpr int kLeftSectionHeight = (kUpperHeight - 2 * kGap) / 3;
constexpr int kAmplifierHeight = 130;
constexpr int kBottomHeight = ProgramPanel::kHeight - kUpperHeight - 3 * kGap;

// Knob columns inside a section.
constexpr int kColumn = NordMetrics::knobWidth + 6;

juce::String text(std::string_view view)
{
    return juce::String(std::string(view));
}

// The name printed next to the control on the synth's panel, inside its section; the full name
// (and the CC) is in the tooltip.
juce::String panelName(C parameter)
{
    switch (parameter) {
    case C::Osc2Semitones: return "Semitones";
    case C::Osc2FineTune: return "Fine Tune";
    case C::OscMix: return "Mix";
    case C::FilterCutoff: return "Frequency";
    case C::FilterResonance: return "Resonance";
    case C::FilterEnvAmount: return "Env Amount";
    case C::PulseWidth: return "Pulse Width";
    case C::FmDepth: return "FM Amount";
    case C::FilterEnvAttack:
    case C::AmpEnvAttack:
    case C::ModEnvAttack: return "Attack";
    case C::FilterEnvDecay:
    case C::AmpEnvDecay:
    case C::ModEnvDecay: return "Decay";
    case C::FilterEnvSustain:
    case C::AmpEnvSustain: return "Sustain";
    case C::FilterEnvRelease:
    case C::AmpEnvRelease: return "Release";
    case C::PortamentoTime: return "Time";
    case C::Gain: return "Gain";
    case C::ModEnvAmount:
    case C::Lfo1Amount: return "Amount";
    case C::Lfo1Rate:
    case C::Lfo2Rate: return "Rate";
    case C::Lfo2Amount: return "Amount / Range";
    case C::Osc1Waveform:
    case C::Osc2Waveform:
    case C::Lfo1Waveform: return "Waveform";
    case C::RingModSync: return "Ring Mod/Sync";
    case C::FilterDistortion: return "Distortion";
    case C::FilterType: return "Filter Type";
    case C::Osc2KeyboardTrack: return "KBD Track";
    case C::FilterKeyboardTrack: return "KBD Track";
    case C::Lfo1Destination:
    case C::ModEnvDestination: return "Destination";
    case C::VoiceMode: return {};
    case C::ModWheelDestination: return {};
    case C::Unison: return "Unison";
    case C::PortamentoAuto: return "Auto";
    case C::FilterVelocity: return "Velocity";
    case C::OctaveShift: return {};
    case C::Lfo2DestinationArpMode: return "Destination / Arp Mode";
    }
    return {};
}

// The number of columns a stepped control's settings are spread over.
int columnsOf(C parameter)
{
    return parameter == C::Lfo2DestinationArpMode ? 2 : 1;
}

juce::String rangeText(int value, int minValue, int maxValue)
{
    const bool outOfRange = value < minValue || value > maxValue;
    return "(" + juce::String(minValue) + " to " + juce::String(maxValue) + ")"
           + (outOfRange ? "  outside the documented range" : "");
}

juce::String nameAndCc(const nl2x::CcParameterInfo& info)
{
    return text(info.name) + " (CC" + juce::String(info.cc) + ")";
}

} // namespace

ProgramPanel::ProgramPanel()
    : masterLevel("Level", 0, 127, false)
{
    for (const auto& info : nl2x::ccParameters()) {
        const auto index = static_cast<std::size_t>(info.id);
        const auto id = info.id;
        switch (info.kind) {
        case nl2x::CcParameterKind::Knob: {
            const bool morphable = nl2x::morphAmountOf(info.byte).has_value();
            auto& knob = knobs[index];
            knob = std::make_unique<ArcKnob>(panelName(id), info.minValue, info.maxValue, morphable);
            knob->onValueEdit = [this, id](int value) {
                if (onValueEdit)
                    onValueEdit(id, value);
            };
            knob->onGestureStart = [this] {
                if (onGestureStart)
                    onGestureStart();
            };
            knob->onGestureEnd = [this] {
                if (onGestureEnd)
                    onGestureEnd();
            };
            if (morphable) {
                const auto byte = info.byte;
                knob->onMorphAmountEdit = [this, byte](int amount) {
                    if (onMorphAmountEdit)
                        onMorphAmountEdit(byte, amount);
                };
            }
            addAndMakeVisible(*knob);
            break;
        }
        case nl2x::CcParameterKind::Stepped: {
            std::vector<juce::String> choices;
            for (const auto& choice : info.choices)
                choices.push_back(text(choice));
            auto& list = lists[index];
            list = std::make_unique<OptionList>(panelName(id), std::move(choices), info.minValue, columnsOf(id));
            list->setTooltip(nameAndCc(info));
            list->onValueEdit = [this, id](int value) {
                if (onValueEdit)
                    onValueEdit(id, value);
            };
            addAndMakeVisible(*list);
            break;
        }
        case nl2x::CcParameterKind::Switch: {
            auto& button = switches[index];
            button = std::make_unique<LedSwitch>(panelName(id));
            button->setTooltip(nameAndCc(info));
            button->onValueEdit = [this, id](int value) {
                if (onValueEdit)
                    onValueEdit(id, value);
            };
            addAndMakeVisible(*button);
            break;
        }
        }
    }

    masterLevel.setEnabled(false);
    masterLevel.setTooltip("Master Level is not sent or received over MIDI: set it on the synth");
    addAndMakeVisible(masterLevel);

    for (auto slot : nl2x::kAllSlots) {
        auto& button = slotButtons[nl2x::slotIndex(slot)];
        button.setButtonText(juce::String::charToString(nl2x::slotLetter(slot)));
        button.setComponentID(juce::String("slotButton") + nl2x::slotLetter(slot));
        button.setClickingTogglesState(true);
        button.setRadioGroupId(kSlotButtonsGroup);
        button.setTooltip(juce::String("Show and edit Slot ") + nl2x::slotLetter(slot));
        button.onClick = [this, slot] {
            if (onSlotChosen)
                onSlotChosen(slot);
        };
        addAndMakeVisible(button);
    }
    slotButtons[0].setToggleState(true, juce::dontSendNotification);

    setSize(kWidth, kHeight);
}

ProgramPanel::~ProgramPanel() = default;

void ProgramPanel::showSlot(nl2x::Slot slot)
{
    slotButtons[nl2x::slotIndex(slot)].setToggleState(true, juce::dontSendNotification);
}

void ProgramPanel::showProgram(const nl2x::Program& program)
{
    for (const auto& info : nl2x::ccParameters()) {
        const auto index = static_cast<std::size_t>(info.id);
        const auto value = nl2x::ccParameterValue(program, info.id);
        if (auto& knob = knobs[index]) {
            knob->setValue(value);
            auto tip = nameAndCc(info) + ": " + juce::String(value) + " " + rangeText(value, info.minValue, info.maxValue);
            if (knob->isMorphable()) {
                const auto amount = nl2x::morphAmountValue(program, info.byte);
                knob->setMorphAmount(amount);
                tip += "\nMorph Amount: " + juce::String(amount) + (amount == 0 ? " (off)" : "")
                       + "\nDrag with Option held (or in Morph mode) to edit it; double-click resets it.";
            }
            knob->setTooltip(tip);
        } else if (auto& list = lists[index]) {
            list->setValue(value);
            if (value < info.minValue || value > info.maxValue)
                list->setTooltip(nameAndCc(info) + ": read from the synth as " + juce::String(value)
                                 + ", outside the documented range; kept unless you choose a setting");
            else
                list->setTooltip(nameAndCc(info));
        } else if (auto& button = switches[index]) {
            button->setValue(value);
        }
    }
}

void ProgramPanel::showNotice(const juce::String& newNotice)
{
    if (notice == newNotice)
        return;
    notice = newNotice;
    const bool showProgramControls = notice.isEmpty();
    for (auto& knob : knobs)
        if (knob)
            knob->setVisible(showProgramControls);
    for (auto& list : lists)
        if (list)
            list->setVisible(showProgramControls);
    for (auto& button : switches)
        if (button)
            button->setVisible(showProgramControls);
    repaint();
}

void ProgramPanel::setMorphMode(bool on)
{
    for (auto& knob : knobs)
        if (knob)
            knob->setMorphMode(on);
}

void ProgramPanel::placeKnob(juce::Component& knob, const Section& section, int x, int y)
{
    knob.setBounds(section.bounds.getX() + x, section.bounds.getY() + y, NordMetrics::knobWidth,
                   NordMetrics::knobHeight);
}

void ProgramPanel::place(C parameter, const Section& section, int x, int y, int width)
{
    const auto index = static_cast<std::size_t>(parameter);
    const auto origin = section.bounds.getPosition() + juce::Point<int>(x, y);
    if (auto& knob = knobs[index]) {
        placeKnob(*knob, section, x, y);
    } else if (auto& list = lists[index]) {
        list->setBounds(origin.x, origin.y, width > 0 ? width : list->preferredWidth(), list->preferredHeight());
    } else if (auto& button = switches[index]) {
        button->setBounds(origin.x, origin.y, width > 0 ? width : button->preferredWidth(), NordMetrics::switchHeight);
    }
}

void ProgramPanel::addSubtitle(const Section& section, const juce::String& subtitle, int x, int y)
{
    subtitles.push_back({ subtitle, section.bounds.getPosition() + juce::Point<int>(x, y) });
}

void ProgramPanel::resized()
{
    sections.clear();
    subtitles.clear();

    const int left = kGap;
    const int centre = left + kLeftWidth + kGap;
    const int right = centre + kCentreWidth + kGap;
    const int top = kGap;
    const int bottom = top + kUpperHeight + kGap;

    const auto section = [this](const juce::String& title, int x, int y, int w, int h, bool program = true) -> Section& {
        return sections.emplace_back(Section { title, { x, y, w, h }, program });
    };

    // Left: LFO 1, LFO 2/Arp, Mod Env.
    {
        const auto& lfo1 = section("LFO 1", left, top, kLeftWidth, kLeftSectionHeight);
        place(C::Lfo1Rate, lfo1, 6, kTop);
        place(C::Lfo1Waveform, lfo1, 6 + kColumn + 4, kTop + 2, 92);
        place(C::Lfo1Destination, lfo1, 6 + kColumn + 100, kTop + 2, 80);
        place(C::Lfo1Amount, lfo1, kLeftWidth - kColumn, kTop);

        const auto& lfo2 = section("LFO 2 / Arp", left, top + kLeftSectionHeight + kGap, kLeftWidth, kLeftSectionHeight);
        place(C::Lfo2Rate, lfo2, 6, kTop);
        place(C::Lfo2DestinationArpMode, lfo2, 6 + kColumn + 4, kTop + 2, 176);
        place(C::Lfo2Amount, lfo2, kLeftWidth - kColumn, kTop);

        const auto& modEnv = section("Mod Env", left, top + 2 * (kLeftSectionHeight + kGap), kLeftWidth,
                                     kLeftSectionHeight);
        place(C::ModEnvAttack, modEnv, 6, kTop);
        place(C::ModEnvDecay, modEnv, 6 + kColumn, kTop);
        place(C::ModEnvDestination, modEnv, 6 + 2 * kColumn + 12, kTop + 2, 90);
        place(C::ModEnvAmount, modEnv, kLeftWidth - kColumn, kTop);
    }

    // Centre: the Oscillators. Osc 1 on the left, Osc 2 on the right, the settings for both below.
    {
        const auto& osc = section("Oscillators", centre, top, kCentreWidth, kUpperHeight);
        const int osc2 = kCentreWidth / 2 + 4;
        addSubtitle(osc, "Osc 1", 8, kTop);
        addSubtitle(osc, "Osc 2", osc2, kTop);
        place(C::Osc1Waveform, osc, 8, kTop + 20, 110);
        place(C::Osc2Waveform, osc, osc2, kTop + 20, 110);
        place(C::FmDepth, osc, 8, kTop + 112);
        place(C::Osc2Semitones, osc, osc2, kTop + 112);
        place(C::Osc2FineTune, osc, osc2 + kColumn, kTop + 112);
        place(C::Osc2KeyboardTrack, osc, osc2, kTop + 112 + NordMetrics::knobHeight + 6, 100);

        const int both = kTop + 250;
        addSubtitle(osc, "Osc 1 + 2", 8, both);
        place(C::RingModSync, osc, 8, both + 26, 130);
        place(C::PulseWidth, osc, osc2, both + 20);
        place(C::OscMix, osc, osc2 + kColumn, both + 20);
    }

    // Right: Amplifier, Filter.
    {
        const auto& amp = section("Amplifier", right, top, kRightWidth, kAmplifierHeight);
        place(C::AmpEnvAttack, amp, 6, kTop);
        place(C::AmpEnvDecay, amp, 6 + kColumn, kTop);
        place(C::AmpEnvSustain, amp, 6 + 2 * kColumn, kTop);
        place(C::AmpEnvRelease, amp, 6 + 3 * kColumn, kTop);
        place(C::Gain, amp, kRightWidth - kColumn, kTop);

        const auto& filter = section("Filter", right, top + kAmplifierHeight + kGap, kRightWidth,
                                     kUpperHeight - kAmplifierHeight - kGap);
        place(C::FilterEnvAttack, filter, 6, kTop);
        place(C::FilterEnvDecay, filter, 6 + kColumn, kTop);
        place(C::FilterEnvSustain, filter, 6 + 2 * kColumn, kTop);
        place(C::FilterEnvRelease, filter, 6 + 3 * kColumn, kTop);
        place(C::FilterType, filter, 6 + 4 * kColumn + 10, kTop + 2, kRightWidth - (6 + 4 * kColumn + 10) - 6);

        const int second = kTop + NordMetrics::knobHeight + 26;
        place(C::FilterCutoff, filter, 6, second);
        place(C::FilterResonance, filter, 6 + kColumn, second);
        place(C::FilterEnvAmount, filter, 6 + 2 * kColumn, second);
        place(C::FilterVelocity, filter, 6 + 3 * kColumn, second + 4, kColumn);
        place(C::FilterKeyboardTrack, filter, 6 + 3 * kColumn + 4, second + 34, kColumn);
        place(C::FilterDistortion, filter, 6 + 4 * kColumn + 10, second + 4, kRightWidth - (6 + 4 * kColumn + 10) - 6);
    }

    // Bottom row, under the columns as on the synth.
    {
        const auto& master = section("Master Level", left, bottom, 104, kBottomHeight, false);
        placeKnob(masterLevel, master, (104 - NordMetrics::knobWidth) / 2, kTop);

        const int slotWidth = kLeftWidth - 104 - kGap;
        const auto& slot = section("Slot", left + 104 + kGap, bottom, slotWidth, kBottomHeight, false);
        const int buttonWidth = 40;
        const int buttonsWidth = 4 * buttonWidth + 3 * 6;
        auto buttons = juce::Rectangle<int>(slot.bounds.getX() + (slotWidth - buttonsWidth) / 2,
                                            slot.bounds.getY() + kTop + 30, buttonsWidth, 32);
        for (auto& button : slotButtons) {
            button.setBounds(buttons.removeFromLeft(buttonWidth));
            buttons.removeFromLeft(6);
        }

        const auto& play = section("Play Mode", centre, bottom, 96, kBottomHeight);
        place(C::VoiceMode, play, 10, kTop, 80);

        const auto& portamento = section("Portamento", centre + 96 + kGap, bottom, 130, kBottomHeight);
        place(C::PortamentoTime, portamento, 4, kTop);
        place(C::PortamentoAuto, portamento, 4 + kColumn - 2, kTop + 34, 58);

        const int unisonX = centre + 96 + 130 + 2 * kGap;
        const auto& unison = section("Unison", unisonX, bottom, centre + kCentreWidth - unisonX, kBottomHeight);
        place(C::Unison, unison, 10, kTop + 34, unison.bounds.getWidth() - 20);

        const auto& octave = section("Oct Shift", right, bottom, 150, kBottomHeight);
        place(C::OctaveShift, octave, 12, kTop - 4, 80);

        const auto& wheel = section("Mod Wheel Dest", right + 150 + kGap, bottom, kRightWidth - 150 - kGap,
                                    kBottomHeight);
        place(C::ModWheelDestination, wheel, 12, kTop - 4, 100);
    }

    // Every CC parameter must have its place (nothing may be lost from the panel).
    for (const auto& info : nl2x::ccParameters()) {
        const auto index = static_cast<std::size_t>(info.id);
        const juce::Component* control = knobs[index] ? static_cast<juce::Component*>(knobs[index].get())
                                         : lists[index] ? static_cast<juce::Component*>(lists[index].get())
                                                        : static_cast<juce::Component*>(switches[index].get());
        jassert(control != nullptr && ! control->getBounds().isEmpty());
        juce::ignoreUnused(control);
    }
}

void ProgramPanel::paint(juce::Graphics& g)
{
    g.fillAll(NordPalette::casingRed);
    g.setColour(NordPalette::background);
    g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(2.0f), NordMetrics::cornerRadius);

    for (const auto& section : sections)
        if (notice.isEmpty() || ! section.program)
            NordLookAndFeel::drawSection(g, section.bounds, section.title);

    if (notice.isNotEmpty()) {
        const auto upper = getLocalBounds().withHeight(kGap + kUpperHeight).reduced(kGap);
        g.setColour(NordPalette::panelGrey);
        g.fillRoundedRectangle(upper.toFloat(), NordMetrics::cornerRadius);
        g.setColour(NordPalette::text);
        g.setFont(NordFonts::heading());
        g.drawFittedText(notice, upper.reduced(24), juce::Justification::centred, 3);
        return;
    }

    g.setFont(NordFonts::controlName());
    g.setColour(NordPalette::text);
    for (const auto& subtitle : subtitles)
        g.drawText(subtitle.text.toUpperCase(), juce::Rectangle<int>(subtitle.position, subtitle.position.translated(140, 16)),
                   juce::Justification::centredLeft, false);
}
