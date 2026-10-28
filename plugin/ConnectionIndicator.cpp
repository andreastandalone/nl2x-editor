#include "ConnectionIndicator.h"

#include "NordLookAndFeel.h"

#include "nl2x/HardwareAssumptions.h"

namespace {

constexpr int kRefreshIntervalMs = 100;

const auto& kGrey = NordPalette::statusOff;
const auto& kAmber = NordPalette::statusBusy;
const auto& kGreen = NordPalette::statusGood;
const auto& kRed = NordPalette::statusBad;

juce::String globalChannelText(nl2x::GlobalChannel channel)
{
    return "Global Channel " + juce::String(channel + nl2x::hardware::kShownGlobalChannelOfDev0);
}

std::pair<juce::Colour, juce::String> describe(NL2XEditorProcessor& processor)
{
    if (! processor.controlsSynth())
        return { kGrey, "Not connected: another instance controls the synth" };

    if (! processor.midiPortsChosen())
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
    const auto dotSize = 10.0f;
    const auto dot = area.removeFromLeft(dotSize + 14.0f).withSizeKeepingCentre(dotSize, dotSize);
    // On a dark disc, so that every state shows on the red header.
    g.setColour(NordPalette::background);
    g.fillEllipse(dot.expanded(3.0f));
    g.setColour(colour);
    g.fillEllipse(dot);
    g.setColour(NordPalette::textOnRed);
    g.setFont(NordFonts::heading());
    g.drawText(text, area, juce::Justification::centredLeft, true);
}
