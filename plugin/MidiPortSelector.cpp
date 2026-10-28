#include "MidiPortSelector.h"

namespace {

constexpr int kNoPortId = 1;
constexpr int kMissingPortId = 2;
constexpr int kFirstPortId = 3;

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

MidiPortSelector::MidiPortSelector(MidiPorts& midiPorts)
    : ports(midiPorts)
{
    inputPorts.onChange = [this] { ports.selectInput(identifierFor(inputPorts, inputIdentifiers)); };
    outputPorts.onChange = [this] { ports.selectOutput(identifierFor(outputPorts, outputIdentifiers)); };

    for (auto* child : std::initializer_list<juce::Component*> { &inputLabel, &outputLabel, &inputPorts, &outputPorts })
        addAndMakeVisible(child);

    ports.addChangeListener(this);
    refresh();
}

MidiPortSelector::~MidiPortSelector()
{
    ports.removeChangeListener(this);
}

void MidiPortSelector::refresh()
{
    inputIdentifiers = fillPortList(inputPorts, juce::MidiInput::getAvailableDevices(), ports.inputIdentifier(),
                                    ports.missingInputName());
    outputIdentifiers = fillPortList(outputPorts, juce::MidiOutput::getAvailableDevices(), ports.outputIdentifier(),
                                     ports.missingOutputName());
    // An instance that does not control the synth must not change the ports of the one that does.
    inputPorts.setEnabled(ports.isOpen());
    outputPorts.setEnabled(ports.isOpen());
}

void MidiPortSelector::resized()
{
    auto area = getLocalBounds();
    inputLabel.setBounds(area.removeFromTop(24));
    inputPorts.setBounds(area.removeFromTop(26));
    area.removeFromTop(12);
    outputLabel.setBounds(area.removeFromTop(24));
    outputPorts.setBounds(area.removeFromTop(26));
}
