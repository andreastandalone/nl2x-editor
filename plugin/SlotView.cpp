#include "SlotView.h"

#include "nl2x/CcParameters.h"
#include "nl2x/HardwareAssumptions.h"
#include "nl2x/ProgramParameters.h"

#include <algorithm>
#include <optional>

namespace {

constexpr int kRefreshIntervalMs = 50;
constexpr int kSlotButtonsGroup = 4401;
constexpr int kRowHeight = 26;
constexpr int kNameWidth = 220;
constexpr int kControlWidth = 280;
constexpr int kHeadingHeight = 32;

juce::String text(std::string_view view)
{
    return juce::String(std::string(view));
}

// "(0 to 127)", and a mark when the stored value lies outside it.
juce::String rangeText(int value, int minValue, int maxValue)
{
    const bool outOfRange = value < minValue || value > maxValue;
    return "(" + juce::String(minValue) + " to " + juce::String(maxValue) + ")"
           + (outOfRange ? "  outside the documented range" : "");
}

juce::String slotName(nl2x::Slot slot)
{
    return "Slot " + juce::String::charToString(nl2x::slotLetter(slot));
}

// "Global Channel 16", counted as the synth shows it (HardwareAssumptions.h).
juce::String shownGlobalChannel(nl2x::GlobalChannel channel)
{
    return "Global Channel " + juce::String(channel + nl2x::hardware::kShownGlobalChannelOfDev0);
}

juce::String statusText(const nl2x::Engine& synth)
{
    const auto channel = synth.globalChannel();
    switch (synth.connectionStatus()) {
    case nl2x::ConnectionStatus::NotConnected:
        return "Not connected: choose the MIDI input and output connected to the synth.";
    case nl2x::ConnectionStatus::SearchingGlobalChannel:
        return "Looking for the synth's Global Channel...";
    case nl2x::ConnectionStatus::ReadingSlots:
        return "Reading from the synth (" + shownGlobalChannel(*channel) + ")...";
    case nl2x::ConnectionStatus::Connected:
        return "Connected, " + shownGlobalChannel(*channel) + ".";
    case nl2x::ConnectionStatus::NoAnswer:
        return "The synth does not answer. Check both MIDI cables and the chosen ports"
               + (channel ? " (last found on " + shownGlobalChannel(*channel) + ")." : juce::String("."));
    }
    return {};
}

} // namespace

// A slider for knobs, a menu of named steps for stepped buttons, a toggle for on/off buttons;
// next to it the stored value as a number, out-of-range values marked. Shows the engine's value
// and edits through the DAW parameter.
class SlotView::ParameterControl final : public juce::Component {
public:
    ParameterControl(NL2XEditorProcessor& owner, const nl2x::CcParameterInfo& parameterInfo)
        : processor(owner),
          info(parameterInfo)
    {
        name.setText(text(info.name) + "  (CC" + juce::String(info.cc) + ")", juce::dontSendNotification);
        addAndMakeVisible(name);
        addAndMakeVisible(number);

        switch (info.kind) {
        case nl2x::CcParameterKind::Knob:
            slider.setSliderStyle(juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 50, kRowHeight - 4);
            slider.setRange(info.minValue, info.maxValue, 1);
            slider.onValueChange = [this] { edit(juce::roundToInt(slider.getValue())); };
            addAndMakeVisible(slider);
            break;
        case nl2x::CcParameterKind::Stepped:
            for (std::size_t i = 0; i < info.choices.size(); ++i)
                box.addItem(juce::String(static_cast<int>(i)) + ": " + text(info.choices[i]), static_cast<int>(i) + 1);
            box.onChange = [this] {
                if (const auto id = box.getSelectedId(); id > 0)
                    edit(info.minValue + id - 1);
            };
            addAndMakeVisible(box);
            break;
        case nl2x::CcParameterKind::Switch:
            toggle.setButtonText("On");
            toggle.onClick = [this] { edit(toggle.getToggleState() ? 1 : 0); };
            addAndMakeVisible(toggle);
            break;
        }
    }

    nl2x::CcParameter parameter() const { return info.id; }

    void show(nl2x::Slot shownSlot, int value)
    {
        slot = shownSlot;
        const bool inRange = value >= info.minValue && value <= info.maxValue;
        number.setText(juce::String(value) + "  " + rangeText(value, info.minValue, info.maxValue),
                       juce::dontSendNotification);
        switch (info.kind) {
        case nl2x::CcParameterKind::Knob:
            // Not while the user drags it: the engine catches up a tick later.
            if (! slider.isMouseButtonDown()) {
                // A value outside the documented range is shown as it is, not at the range's end.
                slider.setRange(std::min(info.minValue, value), std::max(info.maxValue, value), 1);
                slider.setValue(value, juce::dontSendNotification);
            }
            break;
        case nl2x::CcParameterKind::Stepped:
            if (inRange)
                box.setSelectedId(value - info.minValue + 1, juce::dontSendNotification);
            else
                box.setText(juce::String(value) + ": outside the documented range", juce::dontSendNotification);
            break;
        case nl2x::CcParameterKind::Switch:
            toggle.setToggleState(value != 0, juce::dontSendNotification);
            break;
        }
    }

    void resized() override
    {
        auto area = getLocalBounds();
        name.setBounds(area.removeFromLeft(kNameWidth));
        auto control = area.removeFromLeft(kControlWidth).reduced(0, 1);
        slider.setBounds(control);
        box.setBounds(control);
        toggle.setBounds(control);
        area.removeFromLeft(12);
        number.setBounds(area);
    }

private:
    void edit(int value) { processor.editSlotParameter(slot, info.id, value); }

    NL2XEditorProcessor& processor;
    const nl2x::CcParameterInfo& info;
    nl2x::Slot slot = nl2x::Slot::A;
    juce::Label name;
    juce::Label number;
    juce::Slider slider;
    juce::ComboBox box;
    juce::ToggleButton toggle;
};

SlotView::SlotView(NL2XEditorProcessor& owner)
    : processor(owner)
{
    for (auto slot : nl2x::kAllSlots) {
        auto& button = slotButtons[nl2x::slotIndex(slot)];
        button.setButtonText(juce::String::charToString(nl2x::slotLetter(slot)));
        button.setClickingTogglesState(true);
        button.setRadioGroupId(kSlotButtonsGroup);
        button.onClick = [this, slot] { selectSlot(slot); };
        addAndMakeVisible(button);
    }
    slotButtons[0].setToggleState(true, juce::dontSendNotification);

    readButton.setTooltip("Read the selected Slot's Edit Buffer from the synth again");
    readButton.onClick = [this] { processor.readSlotFromSynth(selectedSlot); };
    addAndMakeVisible(readButton);

    noticeLabel.setJustificationType(juce::Justification::centred);
    noticeLabel.setFont(juce::FontOptions(16.0f));
    addChildComponent(noticeLabel);
    addAndMakeVisible(statusLabel);

    for (const auto& info : nl2x::ccParameters())
        content.addAndMakeVisible(*controls.emplace_back(std::make_unique<ParameterControl>(processor, info)));

    morphHeading.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    content.addAndMakeVisible(morphHeading);
    for (const auto& info : nl2x::programParameters()) {
        if (! info.morphedParameter)
            continue;
        auto& row = morphRows.emplace_back();
        row.parameter = info.id;
        row.name = std::make_unique<juce::Label>(juce::String(), text(info.name));
        row.value = std::make_unique<juce::Label>();
        content.addAndMakeVisible(*row.name);
        content.addAndMakeVisible(*row.value);
    }

    viewport.setViewedComponent(&content, false);
    viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport);

    refresh();
    startTimer(kRefreshIntervalMs);
}

SlotView::~SlotView()
{
    stopTimer();
}

void SlotView::resized()
{
    auto area = getLocalBounds();

    auto top = area.removeFromTop(28);
    for (auto& button : slotButtons) {
        button.setBounds(top.removeFromLeft(36));
        top.removeFromLeft(4);
    }
    top.removeFromLeft(12);
    readButton.setBounds(top.removeFromLeft(130));
    top.removeFromLeft(12);
    statusLabel.setBounds(top);

    area.removeFromTop(8);
    viewport.setBounds(area);
    noticeLabel.setBounds(area);
    layOutContent();
}

void SlotView::layOutContent()
{
    const auto width = std::max(600, viewport.getWidth() - viewport.getScrollBarThickness() - 4);
    int y = 0;
    for (auto& control : controls) {
        control->setBounds(0, y, width, kRowHeight - 2);
        y += kRowHeight;
    }
    y += 8;
    morphHeading.setBounds(0, y, width, kHeadingHeight);
    y += kHeadingHeight;
    for (auto& row : morphRows) {
        row.name->setBounds(0, y, kNameWidth + kControlWidth, kRowHeight - 2);
        row.value->setBounds(kNameWidth + kControlWidth + 12, y, width - kNameWidth - kControlWidth - 12, kRowHeight - 2);
        y += kRowHeight;
    }
    content.setSize(width, y + 8);
}

void SlotView::timerCallback()
{
    if (processor.synth().stateRevision() != shownRevision)
        refresh();
}

void SlotView::selectSlot(nl2x::Slot slot)
{
    selectedSlot = slot;
    refresh();
}

void SlotView::refresh()
{
    const auto& synth = processor.synth();
    shownRevision = synth.stateRevision();
    statusLabel.setText(statusText(synth), juce::dontSendNotification);

    const auto& state = synth.slotState(selectedSlot);
    juce::String notice;
    if (state.content == nl2x::SlotContent::PercussionKit)
        notice = slotName(selectedSlot) + " is playing a Percussion Kit";
    else if (state.content == nl2x::SlotContent::Unknown)
        notice = slotName(selectedSlot) + " has not been read from the synth yet";

    noticeLabel.setText(notice, juce::dontSendNotification);
    noticeLabel.setVisible(notice.isNotEmpty());
    viewport.setVisible(notice.isEmpty());
    if (! state.program)
        return;

    for (auto& control : controls)
        control->show(selectedSlot, nl2x::ccParameterValue(*state.program, control->parameter()));
    for (auto& row : morphRows) {
        const auto value = state.program->value(row.parameter);
        row.value->setText(juce::String(value) + "  " + rangeText(value, nl2x::programParameterInfo(row.parameter).minValue, nl2x::programParameterInfo(row.parameter).maxValue), juce::dontSendNotification);
    }
}
