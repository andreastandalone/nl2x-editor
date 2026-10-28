#include "PluginEditor.h"

#include "nl2x/HardwareAssumptions.h"

namespace {

const auto kCutoffParameterId = nl2x::dawParameterId(nl2x::Slot::A, nl2x::ProgramParameter::FilterCutoff);

constexpr int kRefreshIntervalMs = 100;
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
      cutoffAttachment(owner.parameterState(), kCutoffParameterId, cutoffKnob),
      portSelector(owner.midiPorts()),
      slotView(owner),
      performanceView(owner),
      checkView(owner)
{
    for (auto* title : { &slotTitle, &settingsTitle, &synthTitle })
        title->setFont(juce::FontOptions(16.0f, juce::Font::bold));

    cutoffLabel.setText(juce::String(std::string(nl2x::programParameterInfo(nl2x::ProgramParameter::FilterCutoff).name)),
                        juce::dontSendNotification);
    cutoffLabel.setJustificationType(juce::Justification::centred);
    cutoffKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);

    const auto background = getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId);
    tabs.addTab("Slots", background, &slotView, false);
    tabs.addTab("Performance", background, &performanceView, false);

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
             &indicator, &checkButton, &slotTitle, &cutoffKnob, &cutoffLabel, &settingsTitle, &portSelector,
             &globalChannelLabel, &globalChannelChoice, &warningLabel, &synthTitle, &tabs })
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

    setSize(760, 860);
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

    auto header = area.removeFromTop(30);
    checkButton.setBounds(header.removeFromRight(150));
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
    synthTitle.setBounds(area.removeFromTop(24));
    tabs.setBounds(area);
    // The Connection check is shown over the tabs.
    checkView.setBounds(area.withTrimmedTop(-24));

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
