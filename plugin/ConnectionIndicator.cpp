#include "ConnectionIndicator.h"

#include "nl2x/HardwareAssumptions.h"

namespace {

constexpr int kRefreshIntervalMs = 100;

const juce::Colour kGrey { 0xff8a8a8a };
const juce::Colour kAmber { 0xffe0a020 };
const juce::Colour kGreen { 0xff3cb043 };
const juce::Colour kRed { 0xffd83a2e };

juce::String globalChannelText(nl2x::GlobalChannel channel)
{
    return "Global Channel " + juce::String(channel + nl2x::hardware::kShownGlobalChannelOfDev0);
}

std::pair<juce::Colour, juce::String> describe(NL2XEditorProcessor& processor)
{
    if (! processor.controlsSynth())
        return { kGrey, "Not connected: another instance controls the synth" };

    auto& ports = processor.midiPorts();
    if (ports.inputIdentifier().isEmpty() || ports.outputIdentifier().isEmpty())
        return { kGrey, "Not connected" };

    const auto& synth = processor.synth();
    if (synth.requestsSuspended())
        return { kAmber, "Paused while the Probe runs" };

    const auto channel = synth.globalChannel();
    switch (synth.connectionStatus()) {
    case nl2x::ConnectionStatus::NotConnected:
        return { kGrey, "Not connected" };
    case nl2x::ConnectionStatus::SearchingGlobalChannel:
        return { kAmber, synth.manualGlobalChannel() ? "Searching for the synth on "
                                                           + globalChannelText(*synth.manualGlobalChannel()) + "..."
                                                     : juce::String("Searching for the synth...") };
    case nl2x::ConnectionStatus::ReadingSlots:
        return { kAmber, "Reading from the synth, " + globalChannelText(*channel) + "..." };
    case nl2x::ConnectionStatus::Connected:
        return { kGreen, "Connected, " + globalChannelText(*channel) };
    case nl2x::ConnectionStatus::NoAnswer:
        return { kRed, "Not answering" };
    }
    return { kGrey, {} };
}

} // namespace

ConnectionIndicator::ConnectionIndicator(NL2XEditorProcessor& owner)
    : processor(owner)
{
    timerCallback();
    startTimer(kRefreshIntervalMs);
}

ConnectionIndicator::~ConnectionIndicator()
{
    stopTimer();
}

void ConnectionIndicator::timerCallback()
{
    const auto [newColour, newText] = describe(processor);
    if (newColour != colour || newText != text) {
        colour = newColour;
        text = newText;
        setTitle(text);
        repaint();
    }
}

void ConnectionIndicator::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    const auto dotSize = 12.0f;
    const auto dot = area.removeFromLeft(dotSize + 8.0f).withSizeKeepingCentre(dotSize, dotSize);
    g.setColour(colour);
    g.fillEllipse(dot);
    g.setColour(getLookAndFeel().findColour(juce::Label::textColourId));
    g.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    g.drawText(text, area, juce::Justification::centredLeft, true);
}
