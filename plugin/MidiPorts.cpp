#include "MidiPorts.h"

namespace {

// Identifiers can change when an interface moves to another USB port; the name is the fallback.
juce::String findDevice(const juce::Array<juce::MidiDeviceInfo>& devices, const juce::String& identifier,
                        const juce::String& name)
{
    for (const auto& device : devices)
        if (device.identifier == identifier)
            return device.identifier;

    for (const auto& device : devices)
        if (name.isNotEmpty() && device.name == name)
            return device.identifier;

    return {};
}

juce::String nameOf(const juce::Array<juce::MidiDeviceInfo>& devices, const juce::String& identifier)
{
    for (const auto& device : devices)
        if (device.identifier == identifier)
            return device.name;
    return {};
}

} // namespace

juce::PropertiesFile::Options MidiPorts::settingsOptions()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "NL2X Editor";
    options.folderName = "NL2X Editor";
    options.filenameSuffix = ".settings";
    options.osxLibrarySubFolder = "Application Support";
    // Several plugin instances may share the file.
    options.processLock = nullptr;
    return options;
}

MidiPorts::MidiPorts(juce::MidiInputCallback& callback)
    : inputCallback(callback),
      settings(settingsOptions()),
      deviceListConnection(juce::MidiDeviceListConnection::make([this] { handleDeviceListChange(); }))
{
}

void MidiPorts::open()
{
    if (active)
        return;
    active = true;
    reopenSavedPorts();
    sendSynchronousChangeMessage();
}

void MidiPorts::close()
{
    if (! active)
        return;
    active = false;
    input.reset();
    output.reset();
    sendSynchronousChangeMessage();
}

void MidiPorts::handleDeviceListChange()
{
    if (! active)
        return;
    if (input != nullptr && nameOf(juce::MidiInput::getAvailableDevices(), input->getIdentifier()).isEmpty())
        input.reset();
    if (output != nullptr && nameOf(juce::MidiOutput::getAvailableDevices(), output->getIdentifier()).isEmpty())
        output.reset();

    reopenSavedPorts();
    sendSynchronousChangeMessage();
}

void MidiPorts::reopenSavedPorts()
{
    if (input == nullptr) {
        const auto devices = juce::MidiInput::getAvailableDevices();
        const auto identifier = findDevice(devices, settings.getValue(kInputIdKey), settings.getValue(kInputNameKey));
        if (identifier.isNotEmpty() && (input = juce::MidiInput::openDevice(identifier, &inputCallback)) != nullptr)
            input->start();
    }

    if (output == nullptr) {
        const auto devices = juce::MidiOutput::getAvailableDevices();
        const auto identifier = findDevice(devices, settings.getValue(kOutputIdKey), settings.getValue(kOutputNameKey));
        if (identifier.isNotEmpty())
            output = juce::MidiOutput::openDevice(identifier);
    }
}

void MidiPorts::selectInput(const juce::String& identifier)
{
    if (! active || identifier == inputIdentifier())
        return;

    input.reset();
    if (identifier.isNotEmpty() && (input = juce::MidiInput::openDevice(identifier, &inputCallback)) != nullptr)
        input->start();

    settings.setValue(kInputIdKey, identifier);
    settings.setValue(kInputNameKey, nameOf(juce::MidiInput::getAvailableDevices(), identifier));
    settings.saveIfNeeded();
    sendSynchronousChangeMessage();
}

void MidiPorts::selectOutput(const juce::String& identifier)
{
    if (! active || identifier == outputIdentifier())
        return;

    output.reset();
    if (identifier.isNotEmpty())
        output = juce::MidiOutput::openDevice(identifier);

    settings.setValue(kOutputIdKey, identifier);
    settings.setValue(kOutputNameKey, nameOf(juce::MidiOutput::getAvailableDevices(), identifier));
    settings.saveIfNeeded();
    sendSynchronousChangeMessage();
}

void MidiPorts::send(const juce::MidiMessage& message)
{
    if (output != nullptr)
        output->sendMessageNow(message);
}

std::optional<int> MidiPorts::rememberedGlobalChannel() const
{
    const auto channel = settings.getIntValue(kGlobalChannelKey, -1);
    if (channel < 0 || channel > 15)
        return std::nullopt;
    return channel;
}

void MidiPorts::rememberGlobalChannel(int channel)
{
    if (rememberedGlobalChannel() == channel)
        return;
    settings.setValue(kGlobalChannelKey, channel);
    settings.saveIfNeeded();
}

std::optional<int> MidiPorts::manualGlobalChannel() const
{
    const auto channel = settings.getIntValue(kManualGlobalChannelKey, -1);
    if (channel < 0 || channel > 15)
        return std::nullopt;
    return channel;
}

void MidiPorts::setManualGlobalChannel(std::optional<int> channel)
{
    if (manualGlobalChannel() == channel)
        return;
    settings.setValue(kManualGlobalChannelKey, channel ? *channel : -1);
    settings.saveIfNeeded();
}

void MidiPorts::setConnectionCheckShown()
{
    settings.setValue(kConnectionCheckShownKey, true);
    settings.saveIfNeeded();
}
