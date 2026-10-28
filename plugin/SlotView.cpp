#include "SlotView.h"

#include "NordLookAndFeel.h"

namespace {

constexpr int kRefreshIntervalMs = 50;

juce::String slotName(nl2x::Slot slot)
{
    return "Slot " + juce::String::charToString(nl2x::slotLetter(slot));
}

// "Slot A, MIDI channel 1": where this Slot's knob changes go.
juce::String channelText(const nl2x::Engine& synth, nl2x::Slot slot)
{
    const auto channel = synth.slotChannel(slot);
    return slotName(slot) + (channel ? ", MIDI channel " + juce::String(*channel + 1) : juce::String(", MIDI channel off"));
}

} // namespace

SlotView::SlotView(NL2XEditorProcessor& owner)
    : processor(owner)
{
    readButton.setTooltip("Read the selected Slot's Edit Buffer from the synth again");
    readButton.onClick = [this] { processor.readSlotFromSynth(selectedSlot); };
    addAndMakeVisible(readButton);

    morphButton.setComponentID("morph");
    morphButton.setClickingTogglesState(true);
    morphButton.setTooltip("Turn every knob into editing its Morph Amount (Option-drag a knob does it for one drag)");
    morphButton.setColour(juce::TextButton::buttonOnColourId, NordPalette::morph);
    morphButton.setColour(juce::TextButton::textColourOnId, NordPalette::sectionTitleText);
    morphButton.onClick = [this] { panel.setMorphMode(morphButton.getToggleState()); };
    addAndMakeVisible(morphButton);

    statusLabel.setFont(NordFonts::body());
    addAndMakeVisible(statusLabel);
    hintLabel.setFont(NordFonts::small());
    hintLabel.setColour(juce::Label::textColourId, NordPalette::textDim);
    hintLabel.setJustificationType(juce::Justification::centredRight);
    hintLabel.setText("Drag a knob up or down; Shift fine, Option for its Morph Amount", juce::dontSendNotification);
    addAndMakeVisible(hintLabel);

    panel.onSlotChosen = [this](nl2x::Slot slot) {
        if (onSlotChosen)
            onSlotChosen(slot);
        else
            selectSlot(slot);
    };
    panel.onValueEdit = [this](nl2x::CcParameter parameter, int value) {
        processor.editSlotParameter(selectedSlot, parameter, value);
    };
    panel.onMorphAmountEdit = [this](nl2x::ProgramParameter parameter, int amount) {
        processor.editSlotMorphAmount(selectedSlot, parameter, amount);
    };
    // One drag is one undo step (#14).
    panel.onGestureStart = [this] { processor.beginEditGesture(); };
    panel.onGestureEnd = [this] { processor.endEditGesture(); };
    addAndMakeVisible(panel);

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
    auto top = area.removeFromTop(kToolRowHeight);
    readButton.setBounds(top.removeFromLeft(130));
    top.removeFromLeft(NordMetrics::gap);
    morphButton.setBounds(top.removeFromLeft(80));
    top.removeFromLeft(12);
    hintLabel.setBounds(top.removeFromRight(360));
    statusLabel.setBounds(top);
    area.removeFromTop(6);
    panel.setBounds(area.removeFromTop(ProgramPanel::kHeight).withWidth(ProgramPanel::kWidth));
}

void SlotView::timerCallback()
{
    if (processor.synth().stateRevision() != shownRevision)
        refresh();
}

void SlotView::selectSlot(nl2x::Slot slot)
{
    selectedSlot = slot;
    panel.showSlot(slot);
    refresh();
}

void SlotView::refresh()
{
    const auto& synth = processor.synth();
    shownRevision = synth.stateRevision();
    statusLabel.setText(channelText(synth, selectedSlot), juce::dontSendNotification);

    const auto& state = synth.slotState(selectedSlot);
    juce::String notice;
    if (state.content == nl2x::SlotContent::PercussionKit)
        notice = slotName(selectedSlot) + " is playing a Percussion Kit. Kit editing is not available yet; the "
                                          "Library can save and send Kits.";
    else if (state.content == nl2x::SlotContent::Unknown)
        notice = slotName(selectedSlot) + " has not been read from the synth yet.";
    panel.showNotice(notice);
    if (state.program)
        panel.showProgram(*state.program);
}
