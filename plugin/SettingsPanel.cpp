#include "SettingsPanel.h"

#include "NordLookAndFeel.h"

#include "nl2x/HardwareAssumptions.h"

namespace {

constexpr int kRefreshIntervalMs = 250;
// Global Channel menu: Automatic, then the 16 channels as the synth shows them.
constexpr int kAutomaticId = 1;
constexpr int kFirstChannelId = 2;
constexpr int kLabelWidth = 140;

} // namespace

SettingsPanel::SettingsPanel(NL2XEditorProcessor& owner, LibraryPanel& library)
    : processor(owner),
      libraryPanel(library),
      portSelector(owner.midiPorts())
{
    for (auto* heading : { &midiTitle, &libraryTitle, &projectTitle })
        heading->setFont(NordFonts::heading());

    closeButton.setComponentID("settingsClose");
    closeButton.onClick = [this] {
        if (onClose)
            onClose();
    };

    globalChannelChoice.addItem("Automatic", kAutomaticId);
    for (int channel = 0; channel < 16; ++channel)
        globalChannelChoice.addItem(juce::String(channel + nl2x::hardware::kShownGlobalChannelOfDev0),
                                    kFirstChannelId + channel);
    globalChannelChoice.setTooltip("Automatic finds the synth's Global Channel by asking each one");
    globalChannelChoice.onChange = [this] { globalChannelChosen(); };
    globalChannelHint.setFont(NordFonts::small());
    globalChannelHint.setColour(juce::Label::textColourId, NordPalette::textDim);
    globalChannelHint.setJustificationType(juce::Justification::topLeft);
    globalChannelHint.setText("Choose a number only if Automatic fails: the synth shows its Global Channel with "
                              "SHIFT + MIDI CH, then MIDI CH again.",
                              juce::dontSendNotification);

    folderLabel.setFont(NordFonts::small());
    folderLabel.setMinimumHorizontalScale(0.6f);
    chooseButton.setTooltip("Choose the folder of .syx files the Library shows");
    chooseButton.onClick = [this] { libraryPanel.chooseFolder(); };

    sendProjectSoundsToggle.setTooltip("When on, opening a project or loading a preset sends its four Slots and its "
                                       "Performance to the synth's Edit Buffers (never to a Bank). When off, "
                                       "nothing is sent and the synth's sounds replace the project's.");
    sendProjectSoundsToggle.onClick = [this] {
        processor.midiPorts().setSendsProjectSoundsOnLoad(sendProjectSoundsToggle.getToggleState());
    };

    for (auto* child : std::initializer_list<juce::Component*> { &closeButton, &projectTitle, &sendProjectSoundsToggle, &midiTitle, &portSelector,
                                                                 &globalChannelLabel, &globalChannelChoice,
                                                                 &globalChannelHint, &libraryTitle, &folderLabel,
                                                                 &chooseButton })
        addAndMakeVisible(child);

    refresh();
}

SettingsPanel::~SettingsPanel()
{
    stopTimer();
}

void SettingsPanel::visibilityChanged()
{
    if (isVisible()) {
        refresh();
        startTimer(kRefreshIntervalMs);
    } else {
        stopTimer();
    }
}

void SettingsPanel::timerCallback()
{
    refresh();
}

void SettingsPanel::refresh()
{
    const auto manual = processor.synth().manualGlobalChannel();
    globalChannelChoice.setSelectedId(manual ? kFirstChannelId + *manual : kAutomaticId, juce::dontSendNotification);
    globalChannelChoice.setEnabled(processor.controlsSynth());
    sendProjectSoundsToggle.setToggleState(processor.midiPorts().sendsProjectSoundsOnLoad(), juce::dontSendNotification);
    const auto folder = libraryPanel.folder().getFullPathName();
    folderLabel.setText(folder, juce::dontSendNotification);
    folderLabel.setTooltip(folder);
}

void SettingsPanel::globalChannelChosen()
{
    const auto id = globalChannelChoice.getSelectedId();
    std::optional<nl2x::GlobalChannel> channel;
    if (id >= kFirstChannelId)
        channel = static_cast<nl2x::GlobalChannel>(id - kFirstChannelId);
    processor.setManualGlobalChannel(channel);
}

void SettingsPanel::paint(juce::Graphics& g)
{
    g.fillAll(NordPalette::background);
    NordLookAndFeel::drawSection(g, getLocalBounds().reduced(NordMetrics::gap), "Settings");
}

void SettingsPanel::resized()
{
    auto area = getLocalBounds().reduced(NordMetrics::gap).withTrimmedTop(NordMetrics::sectionTitleHeight).reduced(16);
    area = area.withWidth(std::min(area.getWidth(), 560));

    auto top = area.removeFromTop(NordMetrics::buttonHeight);
    closeButton.setBounds(top.removeFromRight(90));
    midiTitle.setBounds(top);
    portSelector.setBounds(area.removeFromTop(MidiPortSelector::kPreferredHeight));
    area.removeFromTop(8);
    auto channelRow = area.removeFromTop(NordMetrics::buttonHeight);
    globalChannelLabel.setBounds(channelRow.removeFromLeft(kLabelWidth));
    globalChannelChoice.setBounds(channelRow.removeFromLeft(140));
    globalChannelHint.setBounds(area.removeFromTop(36).withTrimmedLeft(kLabelWidth));
    area.removeFromTop(16);

    libraryTitle.setBounds(area.removeFromTop(24));
    auto folderRow = area.removeFromTop(NordMetrics::buttonHeight);
    chooseButton.setBounds(folderRow.removeFromRight(90));
    folderRow.removeFromRight(8);
    folderLabel.setBounds(folderRow);
    area.removeFromTop(16);

    projectTitle.setBounds(area.removeFromTop(24));
    sendProjectSoundsToggle.setBounds(area.removeFromTop(NordMetrics::buttonHeight));
}
