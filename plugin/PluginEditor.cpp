#include "PluginEditor.h"

#include "nl2x/HardwareAssumptions.h"

namespace {

const auto kCutoffParameterId = nl2x::dawParameterId(nl2x::Slot::A, nl2x::CcParameter::FilterCutoff);

constexpr int kRefreshIntervalMs = 100;
constexpr int kLibraryWidth = 320;
// Global Channel menu: Automatic, then the 16 channels as the synth shows them.
constexpr int kAutomaticId = 1;
constexpr int kFirstChannelId = 2;

juce::String instanceWarningText(SynthControl::Refusal refusal)
{
    const juce::String where = refusal == SynthControl::Refusal::AnotherApplication
                                   ? "in another application (the NL2X Editor app or another DAW)"
                                   : "in this session";
    return "Another NL2X Editor " + where + " controls the synth. This one sends nothing to it. When the other one "
           "is closed, press Take control.";
}

} // namespace

NL2XPluginEditor::NL2XPluginEditor(NL2XEditorProcessor& owner)
    : AudioProcessorEditor(owner),
      editorProcessor(owner),
      indicator(owner),
      portSelector(owner.midiPorts()),
      selectorBar(owner),
      slotView(owner),
      performanceView(owner),
      checkView(owner),
      libraryPanel(owner, [this] { return slotView.selected(); }),
      memoryPanel(owner, libraryPanel, [this] { return slotView.selected(); })
{
    for (auto* title : { &slotTitle, &settingsTitle, &synthTitle })
        title->setFont(juce::FontOptions(16.0f, juce::Font::bold));

    cutoffLabel.setText(juce::String(std::string(nl2x::ccParameterInfo(nl2x::CcParameter::FilterCutoff).name)),
                        juce::dontSendNotification);
    cutoffLabel.setJustificationType(juce::Justification::centred);
    cutoffKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
    // Through the processor rather than a parameter attachment, so that its edits are the user's
    // and undoable (#14); it shows the DAW parameter.
    const auto& cutoffInfo = nl2x::ccParameterInfo(nl2x::CcParameter::FilterCutoff);
    cutoffKnob.setRange(cutoffInfo.minValue, cutoffInfo.maxValue, 1);
    cutoffKnob.onDragStart = [this] { editorProcessor.beginEditGesture(); };
    cutoffKnob.onDragEnd = [this] { editorProcessor.endEditGesture(); };
    cutoffKnob.onValueChange = [this] {
        editorProcessor.editSlotParameter(nl2x::Slot::A, nl2x::CcParameter::FilterCutoff,
                                          juce::roundToInt(cutoffKnob.getValue()));
    };

    undoButton.setTooltip("Undo the last edit made in the plugin (Cmd+Z)");
    undoButton.onClick = [this] { editorProcessor.undo(); };
    redoButton.setTooltip("Redo the edit undone last (Cmd+Shift+Z)");
    redoButton.onClick = [this] { editorProcessor.redo(); };
    // Cmd+Z and Cmd+Shift+Z reach keyPressed when the host lets the plugin window have them.
    setWantsKeyboardFocus(true);

    const auto background = getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId);
    tabs.addTab("Slots", background, &slotView, false);
    tabs.addTab("Performance", background, &performanceView, false);
    tabs.addTab("Synth memory", background, &memoryPanel, false);

    warningLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
    warningLabel.setJustificationType(juce::Justification::topLeft);

    checkButton.setTooltip("Test the MIDI connection with the synth and explain any problem");
    checkButton.onClick = [this] { editorProcessor.openConnectionCheck(); };

    instanceWarning.setColour(juce::Label::backgroundColourId, juce::Colour(0xffd83a2e).withAlpha(0.3f));
    instanceWarning.setJustificationType(juce::Justification::centredLeft);
    takeControlButton.onClick = [this] {
        editorProcessor.takeControlOfSynth();
        timerCallback();
    };

    globalChannelChoice.addItem("Automatic", kAutomaticId);
    for (int channel = 0; channel < 16; ++channel)
        globalChannelChoice.addItem(juce::String(channel + nl2x::hardware::kShownGlobalChannelOfDev0),
                                    kFirstChannelId + channel);
    globalChannelChoice.setTooltip("Automatic finds the synth's Global Channel by asking each one. Choose a number "
                                   "only if that fails: the synth shows its Global Channel with SHIFT + MIDI CH, "
                                   "then MIDI CH again.");
    globalChannelChoice.onChange = [this] { globalChannelChosen(); };
    refreshGlobalChannelChoice();

    for (auto* child : std::initializer_list<juce::Component*> {
             &indicator, &checkButton, &undoButton, &redoButton, &slotTitle, &cutoffKnob, &cutoffLabel, &settingsTitle, &portSelector,
             &globalChannelLabel, &globalChannelChoice, &warningLabel, &synthTitle, &selectorBar, &tabs,
             &libraryPanel })
        addAndMakeVisible(child);
    addChildComponent(instanceWarning);
    addChildComponent(takeControlButton);
    addChildComponent(checkView);

    // The probe sends raw requests and dumps: a developer tool, never shown inside a DAW.
    if (owner.wrapperType == juce::AudioProcessor::wrapperType_Standalone) {
        probeButton.setTooltip("Hardware discovery: send requests and dumps to the synth and log every reply");
        probeButton.onClick = [this] { showProbe(); };
        addAndMakeVisible(probeButton);
    }

    setSize(760 + kLibraryWidth, 860 + SelectorBar::kPreferredHeight + 8);
    timerCallback();
    startTimer(kRefreshIntervalMs);
}

NL2XPluginEditor::~NL2XPluginEditor()
{
    stopTimer();
}

void NL2XPluginEditor::timerCallback()
{
    const bool controls = editorProcessor.controlsSynth();
    if (instanceWarning.isVisible() == controls) {
        instanceWarning.setVisible(! controls);
        takeControlButton.setVisible(! controls);
        resized();
    }
    if (! controls)
        instanceWarning.setText(instanceWarningText(editorProcessor.controlRefusal()), juce::dontSendNotification);
    globalChannelChoice.setEnabled(controls);
    checkButton.setEnabled(controls);
    undoButton.setEnabled(editorProcessor.canUndo());
    redoButton.setEnabled(editorProcessor.canRedo());
    if (! cutoffKnob.isMouseButtonDown())
        if (auto* cutoff = editorProcessor.parameterState().getParameter(kCutoffParameterId))
            cutoffKnob.setValue(cutoff->convertFrom0to1(cutoff->getValue()), juce::dontSendNotification);

    const bool checkOpen = editorProcessor.connectionCheckOpen();
    if (checkView.isVisible() != checkOpen) {
        checkView.setVisible(checkOpen);
        if (checkOpen)
            checkView.toFront(false);
    }

    const auto& synth = editorProcessor.synth();
    if (synth.stateRevision() != shownRevision) {
        shownRevision = synth.stateRevision();
        juce::StringArray lines;
        for (const auto& warning : synth.slotChannelWarnings())
            lines.add(juce::String(nl2x::slotChannelWarningText(warning)));
        warningLabel.setText(lines.joinIntoString("\n"), juce::dontSendNotification);
    }
}

bool NL2XPluginEditor::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0)) {
        editorProcessor.undo();
        return true;
    }
    if (key == juce::KeyPress('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)) {
        editorProcessor.redo();
        return true;
    }
    return false;
}

void NL2XPluginEditor::refreshGlobalChannelChoice()
{
    const auto manual = editorProcessor.synth().manualGlobalChannel();
    globalChannelChoice.setSelectedId(manual ? kFirstChannelId + *manual : kAutomaticId, juce::dontSendNotification);
}

void NL2XPluginEditor::globalChannelChosen()
{
    const auto id = globalChannelChoice.getSelectedId();
    std::optional<nl2x::GlobalChannel> channel;
    if (id >= kFirstChannelId)
        channel = static_cast<nl2x::GlobalChannel>(id - kFirstChannelId);
    editorProcessor.setManualGlobalChannel(channel);
}

void NL2XPluginEditor::showProbe()
{
    if (probeWindow == nullptr)
        probeWindow = std::make_unique<ProbeWindow>(editorProcessor);
    probeWindow->setVisible(true);
    probeWindow->toFront(true);
}

void NL2XPluginEditor::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void NL2XPluginEditor::resized()
{
    auto area = getLocalBounds().reduced(16);
    libraryPanel.setBounds(area.removeFromRight(kLibraryWidth - 16));
    area.removeFromRight(16);

    auto header = area.removeFromTop(30);
    checkButton.setBounds(header.removeFromRight(150));
    header.removeFromRight(12);
    redoButton.setBounds(header.removeFromRight(60));
    header.removeFromRight(4);
    undoButton.setBounds(header.removeFromRight(60));
    header.removeFromRight(12);
    indicator.setBounds(header);
    area.removeFromTop(8);

    if (instanceWarning.isVisible()) {
        auto banner = area.removeFromTop(48);
        takeControlButton.setBounds(banner.removeFromRight(120).withSizeKeepingCentre(120, 28));
        banner.removeFromRight(8);
        instanceWarning.setBounds(banner);
        area.removeFromTop(8);
    }

    auto top = area.removeFromTop(260);
    area.removeFromTop(8);
    warningLabel.setBounds(area.removeFromTop(44));
    // The Connection check is shown over the selectors and the tabs.
    checkView.setBounds(area);
    synthTitle.setBounds(area.removeFromTop(24));
    selectorBar.setBounds(area.removeFromTop(SelectorBar::kPreferredHeight));
    area.removeFromTop(8);
    tabs.setBounds(area);

    auto slotArea = top.removeFromLeft(160);
    slotTitle.setBounds(slotArea.removeFromTop(24));
    cutoffLabel.setBounds(slotArea.removeFromTop(20));
    cutoffKnob.setBounds(slotArea.removeFromTop(140));

    top.removeFromLeft(24);
    settingsTitle.setBounds(top.removeFromTop(24));
    portSelector.setBounds(top.removeFromTop(MidiPortSelector::kPreferredHeight));
    top.removeFromTop(12);
    auto channelRow = top.removeFromTop(26);
    globalChannelLabel.setBounds(channelRow.removeFromLeft(120));
    globalChannelChoice.setBounds(channelRow.removeFromLeft(140));
    top.removeFromTop(16);
    probeButton.setBounds(top.removeFromTop(26).removeFromLeft(100));
}
