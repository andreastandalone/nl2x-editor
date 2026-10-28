#include "PluginEditor.h"

namespace {

const auto kCutoffParameterId = nl2x::dawParameterId(nl2x::Slot::A, nl2x::ProgramParameter::FilterCutoff);

} // namespace

NL2XPluginEditor::NL2XPluginEditor(NL2XEditorProcessor& owner)
    : AudioProcessorEditor(owner),
      editorProcessor(owner),
      cutoffAttachment(owner.parameterState(), kCutoffParameterId, cutoffKnob),
      portSelector(owner.midiPorts()),
      slotView(owner),
      performanceView(owner)
{
    for (auto* title : { &slotTitle, &settingsTitle })
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

    for (auto* child : std::initializer_list<juce::Component*> { &slotTitle, &cutoffKnob, &cutoffLabel, &settingsTitle,
                                                                 &portSelector, &warningLabel, &tabs })
        addAndMakeVisible(child);

    // The probe sends raw requests and dumps: a developer tool, never shown inside a DAW.
    if (owner.wrapperType == juce::AudioProcessor::wrapperType_Standalone) {
        probeButton.setTooltip("Hardware discovery: send requests and dumps to the synth and log every reply");
        probeButton.onClick = [this] { showProbe(); };
        addAndMakeVisible(probeButton);
    }

    setSize(760, 800);
    timerCallback();
    startTimer(50);
}

NL2XPluginEditor::~NL2XPluginEditor()
{
    stopTimer();
}

void NL2XPluginEditor::timerCallback()
{
    const auto& synth = editorProcessor.synth();
    if (synth.stateRevision() == shownRevision)
        return;
    shownRevision = synth.stateRevision();

    juce::StringArray lines;
    for (const auto& warning : synth.slotChannelWarnings())
        lines.add(juce::String(nl2x::slotChannelWarningText(warning)));
    warningLabel.setText(lines.joinIntoString("\n"), juce::dontSendNotification);
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

    auto synthArea = area.removeFromBottom(getHeight() - 300);
    warningLabel.setBounds(synthArea.removeFromTop(44));
    tabs.setBounds(synthArea);

    auto slotArea = area.removeFromLeft(160);
    slotTitle.setBounds(slotArea.removeFromTop(24));
    cutoffLabel.setBounds(slotArea.removeFromTop(20));
    cutoffKnob.setBounds(slotArea.removeFromTop(140));

    area.removeFromLeft(24);
    settingsTitle.setBounds(area.removeFromTop(24));
    portSelector.setBounds(area.removeFromTop(MidiPortSelector::kPreferredHeight));
    area.removeFromTop(16);
    probeButton.setBounds(area.removeFromTop(26).removeFromLeft(100));
}
