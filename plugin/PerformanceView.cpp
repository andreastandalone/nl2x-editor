#include "PerformanceView.h"

#include "nl2x/HardwareAssumptions.h"
#include "nl2x/PerformanceSettings.h"

#include <algorithm>
#include <optional>

namespace {

constexpr int kRefreshIntervalMs = 50;
constexpr int kRowHeight = 26;
constexpr int kLabelWidth = 250;
constexpr int kHeadingHeight = 32;
// ComboBox item IDs of the override menu: "From the Performance", then channels 1–16.
constexpr int kNoOverrideId = 1;
constexpr int kFirstChannelId = 2;

using Info = nl2x::PerformanceSettingInfo;

juce::String text(std::string_view view)
{
    return juce::String(std::string(view));
}

// The value as the user reads it: the manual's name for it where there is one.
juce::String valueText(const Info& info, int value)
{
    if (value >= info.minValue && value <= info.maxValue) {
        if (! info.choices.empty())
            return text(info.choices[static_cast<std::size_t>(value - info.minValue)]);
        if (value == info.minValue && ! info.lowestValueName.empty())
            return text(info.lowestValueName);
        if (info.display == nl2x::PerformanceSettingDisplay::MidiChannel)
            return juce::String(value + 1);
        if (info.display == nl2x::PerformanceSettingDisplay::Note)
            return juce::MidiMessage::getMidiNoteName(value, true, true, 3) + " (" + juce::String(value) + ")";
        return juce::String(value);
    }
    return juce::String(value) + " (outside the documented range)";
}

juce::String channelText(std::optional<nl2x::MidiChannel> channel)
{
    return channel ? juce::String(*channel + 1) : juce::String("off");
}

juce::String statusText(const nl2x::Engine& synth)
{
    const bool haveOne = synth.performance().has_value();
    switch (synth.performanceStatus()) {
    case nl2x::PerformanceStatus::NotRead:
        return "Not read from the synth yet: Performance edits are not sent until it is.";
    case nl2x::PerformanceStatus::Reading:
        return "Reading the Performance from the synth...";
    case nl2x::PerformanceStatus::Read:
        return "Read from the synth. Changes go to its Performance Edit Buffer.";
    case nl2x::PerformanceStatus::NoAnswer:
        return haveOne ? "The synth did not answer; showing the Performance read earlier."
                       : "The synth did not answer the Performance request: Slot Channels are the defaults or "
                         "your overrides, and Performance edits are not sent.";
    }
    return {};
}

} // namespace

// A toggle for on/off settings, a menu for named steps and MIDI channels, a number box otherwise.
class PerformanceView::SettingControl final : public juce::Component {
public:
    SettingControl(NL2XEditorProcessor& owner, const Info& settingInfo, nl2x::Slot settingSlot)
        : processor(owner),
          info(settingInfo),
          slot(settingSlot)
    {
        if (info.minValue == 0 && info.maxValue == 1) {
            kind = Kind::Toggle;
            toggle.onClick = [this] { edit(toggle.getToggleState() ? 1 : 0); };
            addAndMakeVisible(toggle);
        } else if (! info.choices.empty() || info.display == nl2x::PerformanceSettingDisplay::MidiChannel) {
            kind = Kind::Choice;
            for (int value = info.minValue; value <= info.maxValue; ++value)
                box.addItem(valueText(info, value), value - info.minValue + 1);
            box.onChange = [this] {
                if (const auto id = box.getSelectedId(); id > 0)
                    edit(info.minValue + id - 1);
            };
            addAndMakeVisible(box);
        } else {
            kind = Kind::Number;
            slider.setSliderStyle(juce::Slider::IncDecButtons);
            slider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 90, kRowHeight - 4);
            slider.setRange(info.minValue, info.maxValue, 1);
            slider.textFromValueFunction = [this](double value) { return valueText(info, juce::roundToInt(value)); };
            slider.valueFromTextFunction = [](const juce::String& entered) { return entered.getDoubleValue(); };
            slider.onValueChange = [this] { edit(juce::roundToInt(slider.getValue())); };
            addAndMakeVisible(slider);
        }
    }

    // `value` empty when no Performance has been read.
    void show(std::optional<int> value, bool enabled)
    {
        setEnabled(enabled);
        const bool inRange = value && *value >= info.minValue && *value <= info.maxValue;
        const auto tip = value && ! inRange ? "Read from the synth as " + juce::String(*value)
                                                  + ", outside the documented range; kept unless you change it"
                                            : juce::String();
        toggle.setTooltip(tip);
        box.setTooltip(tip);
        slider.setTooltip(tip);
        switch (kind) {
        case Kind::Toggle:
            toggle.setToggleState(value && *value != 0, juce::dontSendNotification);
            break;
        case Kind::Choice:
            if (! value)
                box.setSelectedId(0, juce::dontSendNotification);
            else if (inRange)
                box.setSelectedId(*value - info.minValue + 1, juce::dontSendNotification);
            else
                box.setText(valueText(info, *value), juce::dontSendNotification);
            break;
        case Kind::Number:
            // A value outside the documented range is shown as it is, not at the range's end.
            const auto shown = value.value_or(info.minValue);
            slider.setRange(std::min(info.minValue, shown), std::max(info.maxValue, shown), 1);
            slider.setValue(shown, juce::dontSendNotification);
            break;
        }
    }

    void resized() override
    {
        toggle.setBounds(getLocalBounds());
        box.setBounds(getLocalBounds().reduced(0, 1));
        slider.setBounds(getLocalBounds());
    }

private:
    enum class Kind { Toggle, Choice, Number };

    void edit(int value) { processor.editPerformanceSetting(info.id, slot, value); }

    NL2XEditorProcessor& processor;
    const Info& info;
    nl2x::Slot slot;
    Kind kind = Kind::Number;
    juce::ToggleButton toggle;
    juce::ComboBox box;
    juce::Slider slider;
};

PerformanceView::PerformanceView(NL2XEditorProcessor& owner)
    : processor(owner)
{
    readButton.setTooltip("Read the synth's Performance Edit Buffer again, and with it the Slot Channels");
    readButton.onClick = [this] { processor.readPerformanceFromSynth(); };
    addAndMakeVisible(readButton);
    addAndMakeVisible(statusLabel);

    viewport.setViewedComponent(&content, false);
    viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport);

    addHeading("Slot Channels");
    for (auto slot : nl2x::kAllSlots) {
        const auto index = nl2x::slotIndex(slot);
        slotColumnLabels[index].setText("Slot " + juce::String::charToString(nl2x::slotLetter(slot)),
                                        juce::dontSendNotification);
        slotColumnLabels[index].setFont(juce::FontOptions(14.0f, juce::Font::bold));
        content.addAndMakeVisible(slotColumnLabels[index]);
        content.addAndMakeVisible(performanceChannels[index]);
        content.addAndMakeVisible(sendChannels[index]);

        auto& box = overrideBoxes[index];
        box.addItem("From the Performance", kNoOverrideId);
        for (int channel = 0; channel < 16; ++channel)
            box.addItem("Channel " + juce::String(channel + 1), kFirstChannelId + channel);
        box.setTooltip("Send this Slot's knob changes on a fixed MIDI channel instead of the one in the "
                       "synth's Performance (saved with the project)");
        box.onChange = [this, slot, &box] {
            const auto id = box.getSelectedId();
            processor.setSlotChannelOverride(
                slot, id >= kFirstChannelId ? std::optional<nl2x::MidiChannel>(static_cast<nl2x::MidiChannel>(id - kFirstChannelId))
                                            : std::nullopt);
        };
        content.addAndMakeVisible(box);
    }
    for (auto* title : { &performanceChannelsTitle, &overridesTitle, &sendChannelsTitle })
        content.addAndMakeVisible(*title);

    addHeading("Performance settings (sent to the synth)");
    for (const auto& info : nl2x::performanceSettings())
        // The Slot Channels have their own section above.
        if (info.access == nl2x::PerformanceSettingAccess::Editable && info.id != nl2x::PerformanceSetting::SlotChannel)
            addRow(info);

    addHeading("Shown only (not sent: the synth does not take these from a Performance)");
    for (const auto& info : nl2x::performanceSettings())
        if (info.access != nl2x::PerformanceSettingAccess::Editable)
            addRow(info);

    refresh();
    startTimer(kRefreshIntervalMs);
}

PerformanceView::~PerformanceView()
{
    stopTimer();
}

juce::Label& PerformanceView::addHeading(const juce::String& title)
{
    auto& heading = *headings.emplace_back(std::make_unique<juce::Label>(juce::String(), title));
    heading.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    content.addAndMakeVisible(heading);
    return heading;
}

PerformanceView::SettingRow& PerformanceView::addRow(const Info& info)
{
    const bool editable = info.access == nl2x::PerformanceSettingAccess::Editable;
    auto& rows = editable ? editableRows : shownOnlyRows;
    auto& row = rows.emplace_back();
    row.info = &info;
    row.label = std::make_unique<juce::Label>(juce::String(), text(info.name));
    if (info.access == nl2x::PerformanceSettingAccess::NotReceived)
        row.label->setTooltip("Not received by the synth in a Performance dump: change it on the synth");
    content.addAndMakeVisible(*row.label);

    if (editable) {
        const auto count = info.perSlot ? nl2x::kAllSlots.size() : 1;
        for (std::size_t i = 0; i < count; ++i) {
            auto& control = *row.controls.emplace_back(std::make_unique<SettingControl>(processor, info, nl2x::kAllSlots[i]));
            content.addAndMakeVisible(control);
        }
    } else {
        row.valueText = std::make_unique<juce::Label>();
        content.addAndMakeVisible(*row.valueText);
    }
    return row;
}

void PerformanceView::resized()
{
    auto area = getLocalBounds();
    auto top = area.removeFromTop(28);
    readButton.setBounds(top.removeFromLeft(130));
    top.removeFromLeft(12);
    statusLabel.setBounds(top);
    area.removeFromTop(8);
    viewport.setBounds(area);
    layOutContent();
}

void PerformanceView::layOutContent()
{
    const auto width = std::max(400, viewport.getWidth() - viewport.getScrollBarThickness() - 4);
    const auto columnWidth = (width - kLabelWidth) / 4;
    int y = 0;

    const auto column = [&](std::size_t index) {
        return juce::Rectangle<int>(kLabelWidth + static_cast<int>(index) * columnWidth, y, columnWidth - 6, kRowHeight - 2);
    };
    const auto nextRow = [&](juce::Component* label) {
        if (label != nullptr)
            label->setBounds(0, y, kLabelWidth - 8, kRowHeight - 2);
    };

    std::size_t heading = 0;
    headings[heading++]->setBounds(0, y, width, kHeadingHeight);
    y += kHeadingHeight;
    for (std::size_t i = 0; i < 4; ++i)
        slotColumnLabels[i].setBounds(column(i));
    y += kRowHeight;
    nextRow(&performanceChannelsTitle);
    for (std::size_t i = 0; i < 4; ++i)
        performanceChannels[i].setBounds(column(i));
    y += kRowHeight;
    nextRow(&overridesTitle);
    for (std::size_t i = 0; i < 4; ++i)
        overrideBoxes[i].setBounds(column(i));
    y += kRowHeight;
    nextRow(&sendChannelsTitle);
    for (std::size_t i = 0; i < 4; ++i)
        sendChannels[i].setBounds(column(i));
    y += kRowHeight;

    headings[heading++]->setBounds(0, y, width, kHeadingHeight);
    y += kHeadingHeight;
    for (auto& row : editableRows) {
        nextRow(row.label.get());
        if (row.info->perSlot) {
            for (std::size_t i = 0; i < row.controls.size(); ++i)
                row.controls[i]->setBounds(column(i));
        } else {
            row.controls.front()->setBounds(column(0).withWidth(std::max(columnWidth * 2 - 6, 160)));
        }
        y += kRowHeight;
    }

    headings[heading++]->setBounds(0, y, width, kHeadingHeight);
    y += kHeadingHeight;
    for (auto& row : shownOnlyRows) {
        nextRow(row.label.get());
        row.valueText->setBounds(kLabelWidth, y, width - kLabelWidth, kRowHeight - 2);
        y += kRowHeight;
    }

    content.setSize(width, y + 8);
}

void PerformanceView::timerCallback()
{
    if (processor.synth().stateRevision() != shownRevision)
        refresh();
}

void PerformanceView::refresh()
{
    const auto& synth = processor.synth();
    shownRevision = synth.stateRevision();
    statusLabel.setText(statusText(synth), juce::dontSendNotification);

    const auto& performance = synth.performance();
    for (auto slot : nl2x::kAllSlots) {
        const auto index = nl2x::slotIndex(slot);
        juce::String fromPerformance = "not read";
        if (performance) {
            const auto stored = performance->setting(nl2x::PerformanceSetting::SlotChannel, slot);
            fromPerformance = channelText(nl2x::hardware::slotChannelFromPerformance(static_cast<std::int8_t>(stored)));
        }
        performanceChannels[index].setText(fromPerformance, juce::dontSendNotification);
        sendChannels[index].setText(channelText(synth.slotChannel(slot)), juce::dontSendNotification);
        const auto pinned = synth.slotChannelOverride(slot);
        overrideBoxes[index].setSelectedId(pinned ? kFirstChannelId + *pinned : kNoOverrideId, juce::dontSendNotification);
    }

    for (auto& row : editableRows)
        for (std::size_t i = 0; i < row.controls.size(); ++i) {
            const auto slot = nl2x::kAllSlots[i];
            row.controls[i]->show(performance ? std::optional<int>(performance->setting(row.info->id, slot)) : std::nullopt,
                                  performance.has_value());
        }

    for (auto& row : shownOnlyRows) {
        juce::String shown = "not read";
        if (performance) {
            if (row.info->perSlot) {
                juce::StringArray parts;
                for (auto slot : nl2x::kAllSlots)
                    parts.add(juce::String::charToString(nl2x::slotLetter(slot)) + ": "
                              + valueText(*row.info, performance->setting(row.info->id, slot)));
                shown = parts.joinIntoString(",  ");
            } else {
                shown = valueText(*row.info, performance->setting(row.info->id));
            }
        }
        row.valueText->setText(shown, juce::dontSendNotification);
    }
}
