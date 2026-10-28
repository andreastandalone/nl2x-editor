#include "PluginEditor.h"

namespace {

constexpr int kNoPortId = 1;
constexpr int kMissingPortId = 2;
constexpr int kFirstPortId = 3;

const auto kCutoffParameterId = nl2x::dawParameterId(nl2x::Slot::A, nl2x::ProgramParameter::FilterCutoff);

// Fills `box` with "None", the available ports and, if the saved port is unplugged, a disabled
// entry naming it; selects the current choice. Returns the identifiers in item order.
juce::StringArray fillPortList(juce::ComboBox& box, const juce::Array<juce::MidiDeviceInfo>& devices,
                               const juce::String& currentIdentifier, const juce::String& missingName)
{
    box.clear(juce::dontSendNotification);
    box.addItem("None", kNoPortId);
    if (missingName.isNotEmpty()) {
        box.addItem(missingName + " (not connected)", kMissingPortId);
        box.setItemEnabled(kMissingPortId, false);
    }

    juce::StringArray identifiers;
    int selectedId = missingName.isNotEmpty() ? kMissingPortId : kNoPortId;
    for (const auto& device : devices) {
        const auto itemId = kFirstPortId + identifiers.size();
        box.addItem(device.name, itemId);
        if (device.identifier == currentIdentifier)
            selectedId = itemId;
        identifiers.add(device.identifier);
    }

    box.setSelectedId(selectedId, juce::dontSendNotification);
    return identifiers;
}

juce::String identifierFor(const juce::ComboBox& box, const juce::StringArray& identifiers)
{
    const auto index = box.getSelectedId() - kFirstPortId;
    return juce::isPositiveAndBelow(index, identifiers.size()) ? identifiers[index] : juce::String();
}

} // namespace

NL2XPluginEditor::NL2XPluginEditor(NL2XEditorProcessor& owner)
    : AudioProcessorEditor(owner),
      editorProcessor(owner),
      cutoffAttachment(owner.parameterState(), kCutoffParameterId, cutoffKnob)
{
    for (auto* title : { &slotTitle, &settingsTitle })
        title->setFont(juce::FontOptions(16.0f, juce::Font::bold));

    cutoffLabel.setText(juce::String(std::string(nl2x::programParameterInfo(nl2x::ProgramParameter::FilterCutoff).name)),
                        juce::dontSendNotification);
    cutoffLabel.setJustificationType(juce::Justification::centred);
    cutoffKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);

    inputPorts.onChange = [this] {
        editorProcessor.midiPorts().selectInput(identifierFor(inputPorts, inputIdentifiers));
        refreshPortLists();
    };
    outputPorts.onChange = [this] {
        editorProcessor.midiPorts().selectOutput(identifierFor(outputPorts, outputIdentifiers));
        refreshPortLists();
    };
    editorProcessor.midiPorts().onPortsChanged = [this] { refreshPortLists(); };

    for (auto* child : std::initializer_list<juce::Component*> { &slotTitle, &cutoffKnob, &cutoffLabel, &settingsTitle,
                                                                 &inputLabel, &outputLabel, &inputPorts, &outputPorts })
        addAndMakeVisible(child);

    refreshPortLists();
    setSize(520, 300);
}

NL2XPluginEditor::~NL2XPluginEditor()
{
    editorProcessor.midiPorts().onPortsChanged = nullptr;
}

void NL2XPluginEditor::refreshPortLists()
{
    auto& ports = editorProcessor.midiPorts();
    inputIdentifiers = fillPortList(inputPorts, juce::MidiInput::getAvailableDevices(), ports.inputIdentifier(),
                                    ports.missingInputName());
    outputIdentifiers = fillPortList(outputPorts, juce::MidiOutput::getAvailableDevices(), ports.outputIdentifier(),
                                     ports.missingOutputName());
}

void NL2XPluginEditor::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void NL2XPluginEditor::resized()
{
    auto area = getLocalBounds().reduced(16);

    auto slotArea = area.removeFromLeft(160);
    slotTitle.setBounds(slotArea.removeFromTop(24));
    cutoffLabel.setBounds(slotArea.removeFromTop(20));
    cutoffKnob.setBounds(slotArea.removeFromTop(140));

    area.removeFromLeft(24);
    settingsTitle.setBounds(area.removeFromTop(24));
    inputLabel.setBounds(area.removeFromTop(24));
    inputPorts.setBounds(area.removeFromTop(26));
    area.removeFromTop(12);
    outputLabel.setBounds(area.removeFromTop(24));
    outputPorts.setBounds(area.removeFromTop(26));
}
