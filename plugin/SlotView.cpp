#include "SlotView.h"

#include "nl2x/HardwareAssumptions.h"
#include "nl2x/ProgramParameters.h"

namespace {

constexpr int kRefreshIntervalMs = 50;
constexpr int kSlotButtonsGroup = 4401;

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

    valueList.setRowHeight(20);
    addAndMakeVisible(statusLabel);
    addAndMakeVisible(valueList);

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
    valueList.setBounds(area);
    noticeLabel.setBounds(area);
}

int SlotView::getNumRows()
{
    return static_cast<int>(nl2x::kProgramParameterCount);
}

void SlotView::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool)
{
    const auto& program = processor.synth().slotState(selectedSlot).program;
    if (! program || row < 0 || row >= getNumRows())
        return;

    const auto& info = nl2x::programParameters()[static_cast<std::size_t>(row)];
    const auto value = program->value(info.id);
    const bool outOfRange = value < info.minValue || value > info.maxValue;

    if (row % 2 != 0)
        g.fillAll(getLookAndFeel().findColour(juce::ListBox::backgroundColourId).brighter(0.05f));

    const auto text = getLookAndFeel().findColour(juce::Label::textColourId);
    auto area = juce::Rectangle<int>(0, 0, width, height).reduced(8, 0);
    g.setFont(juce::FontOptions(14.0f));
    g.setColour(text);
    g.drawText(juce::String(std::string(info.name)), area.removeFromLeft(width / 2), juce::Justification::centredLeft);
    g.drawText(juce::String(value), area.removeFromLeft(60), juce::Justification::centredRight);
    g.setColour(text.withAlpha(0.5f));
    // Values outside the documented range are shown as they are, and marked.
    g.drawText("(" + juce::String(info.minValue) + " to " + juce::String(info.maxValue) + ")"
                   + (outOfRange ? "  outside the documented range" : ""),
               area.reduced(12, 0), juce::Justification::centredLeft);
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
    valueList.setVisible(notice.isEmpty());
    valueList.updateContent();
    valueList.repaint();
}
