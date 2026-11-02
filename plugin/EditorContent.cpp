#include "EditorContent.h"

#include "NordLookAndFeel.h"

namespace {

constexpr int kRefreshIntervalMs = 100;
constexpr int kMargin = 12;
constexpr int kLibraryWidth = 270;
constexpr int kTabsHeight = 30;
constexpr int kWarningsHeight = 22;
constexpr int kViewWidth = ProgramPanel::kWidth;
constexpr int kEditorWidth = kMargin + kViewWidth + kMargin + kLibraryWidth + kMargin;
constexpr int kEditorHeight = NordMetrics::headerHeight + NordMetrics::gap + kTabsHeight + NordMetrics::gap
                              + SelectorBar::kPreferredHeight + kWarningsHeight + SlotView::kPreferredHeight + kMargin;

juce::String instanceWarningText(SynthControl::Refusal refusal)
{
    const juce::String where = refusal == SynthControl::Refusal::AnotherApplication
                                   ? "in another application (the NL2X Editor app or another DAW)"
                                   : "in this session";
    return "Another NL2X Editor " + where + " controls the synth. This one sends nothing to it. When the other one "
           "is closed, press Take control.";
}

} // namespace

const int EditorContent::kWidth = kEditorWidth;
const int EditorContent::kHeight = kEditorHeight;

EditorContent::EditorContent(NL2XEditorProcessor& owner)
    : editorProcessor(owner),
      indicator(owner),
      selectorBar(owner),
      slotView(owner),
      performanceView(owner),
      libraryPanel(owner, [this] { return slotView.selected(); }),
      memoryPanel(owner, libraryPanel, [this] { return slotView.selected(); }),
      checkView(owner),
      settingsPanel(owner, libraryPanel)
{
    undoButton.setTooltip("Undo the last edit made in the plugin (Cmd+Z)");
    undoButton.onClick = [this] { editorProcessor.undo(); };
    redoButton.setTooltip("Redo the edit undone last (Cmd+Shift+Z)");
    redoButton.onClick = [this] { editorProcessor.redo(); };

    checkButton.setTooltip("Test the MIDI connection with the synth and explain any problem");
    checkButton.onClick = [this] {
        showSettings(false);
        editorProcessor.openConnectionCheck();
    };
    settingsButton.setComponentID("settings");
    settingsButton.setTooltip("MIDI ports, Global Channel and Library folder");
    settingsButton.onClick = [this] { showSettings(! settingsPanel.isVisible()); };
    settingsPanel.onClose = [this] { showSettings(false); };
    aboutButton.setComponentID("about");
    aboutButton.setTooltip("Version, licence and credits");
    aboutButton.onClick = [this] { showAbout(! aboutPanel.isVisible()); };
    aboutPanel.onClose = [this] { showAbout(false); };

    tabs.setComponentID("viewTabs");
    for (auto slot : nl2x::kAllSlots)
        tabs.addTab(juce::String("Slot ") + nl2x::slotLetter(slot), NordPalette::casingRed, -1);
    tabs.addTab("Performance", NordPalette::casingRed, -1);
    tabs.addTab("Synth memory", NordPalette::casingRed, -1);
    tabs.setCurrentTabIndex(0, false);
    tabs.onTabChosen = [this](int index) { showTab(index); };

    warningLabel.setColour(juce::Label::textColourId, NordPalette::warning);
    warningLabel.setFont(NordFonts::body());
    warningLabel.setJustificationType(juce::Justification::centredLeft);
    warningLabel.setMinimumHorizontalScale(0.7f);
    instanceWarning.setColour(juce::Label::backgroundColourId, NordPalette::casingRed.withAlpha(0.3f));
    instanceWarning.setFont(NordFonts::body());
    instanceWarning.setJustificationType(juce::Justification::centredLeft);
    takeControlButton.onClick = [this] {
        editorProcessor.takeControlOfSynth();
        timerCallback();
    };

    for (auto* child : std::initializer_list<juce::Component*> { &indicator, &checkButton, &undoButton, &redoButton,
                                                                 &settingsButton, &aboutButton, &tabs, &selectorBar, &warningLabel,
                                                                 &slotView, &libraryPanel })
        addAndMakeVisible(child);
    for (auto* child : std::initializer_list<juce::Component*> { &instanceWarning, &takeControlButton,
                                                                 &performanceView, &memoryPanel, &checkView,
                                                                 &settingsPanel, &aboutPanel })
        addChildComponent(child);

    // The probe sends raw requests and dumps: a developer tool, never shown inside a DAW.
    if (owner.wrapperType == juce::AudioProcessor::wrapperType_Standalone) {
        probeButton.setTooltip("Hardware discovery: send requests and dumps to the synth and log every reply");
        probeButton.onClick = [this] { showProbe(); };
        addAndMakeVisible(probeButton);
    }

    setSize(kEditorWidth, kEditorHeight);
    timerCallback();
    startTimer(kRefreshIntervalMs);
}

EditorContent::~EditorContent()
{
    stopTimer();
}

void EditorContent::showTab(int index)
{
    if (index >= 0 && index < kPerformanceTab)
        slotView.selectSlot(nl2x::kAllSlots[static_cast<std::size_t>(index)]);
    slotView.setVisible(index < kPerformanceTab);
    performanceView.setVisible(index == kPerformanceTab);
    memoryPanel.setVisible(index == kMemoryTab);
}

void EditorContent::showAbout(bool show)
{
    if (show)
        showSettings(false);
    aboutPanel.setVisible(show);
    aboutButton.setToggleState(show, juce::dontSendNotification);
    if (show)
        aboutPanel.toFront(false);
}

void EditorContent::showSettings(bool show)
{
    if (show)
        showAbout(false);
    settingsPanel.setVisible(show);
    settingsButton.setToggleState(show, juce::dontSendNotification);
    if (show)
        settingsPanel.toFront(false);
}

void EditorContent::timerCallback()
{
    const bool controls = editorProcessor.controlsSynth();
    if (instanceWarning.isVisible() == controls) {
        instanceWarning.setVisible(! controls);
        takeControlButton.setVisible(! controls);
        warningLabel.setVisible(controls);
    }
    if (! controls)
        instanceWarning.setText(instanceWarningText(editorProcessor.controlRefusal()), juce::dontSendNotification);
    checkButton.setEnabled(controls);
    undoButton.setEnabled(editorProcessor.canUndo());
    redoButton.setEnabled(editorProcessor.canRedo());

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
        // One line; the tooltip holds them all should the line be too short.
        warningLabel.setText(lines.joinIntoString("   "), juce::dontSendNotification);
        warningLabel.setTooltip(lines.joinIntoString("\n"));
    }
}

void EditorContent::showProbe()
{
    if (probeWindow == nullptr)
        probeWindow = std::make_unique<ProbeWindow>(editorProcessor);
    probeWindow->setVisible(true);
    probeWindow->toFront(true);
}

void EditorContent::paint(juce::Graphics& g)
{
    g.fillAll(NordPalette::background);
    auto header = getLocalBounds().removeFromTop(NordMetrics::headerHeight);
    g.setColour(NordPalette::headerBar);
    g.fillRect(header);
    g.setColour(NordPalette::casingRed);
    g.fillRect(header.removeFromBottom(2));

    // The name: "NL2X" in the accent, "EDITOR" in the text colour.
    const juce::Font font(NordFonts::header());
    g.setFont(font);
    auto name = header.withTrimmedLeft(kMargin).withWidth(160).toFloat();
    const juce::String first = "NL2X";
    g.setColour(NordPalette::casingRed);
    g.drawText(first, name, juce::Justification::centredLeft, false);
    name.removeFromLeft(juce::GlyphArrangement::getStringWidth(font, first + " "));
    g.setColour(NordPalette::text);
    g.drawText("EDITOR", name, juce::Justification::centredLeft, false);
}

void EditorContent::resized()
{
    auto area = getLocalBounds();

    auto header = area.removeFromTop(NordMetrics::headerHeight).reduced(kMargin, 9);
    header.removeFromLeft(150);
    const auto headerButton = [&header](juce::Component& button, int width) {
        button.setBounds(header.removeFromRight(width));
        header.removeFromRight(6);
    };
    if (probeButton.isVisible())
        headerButton(probeButton, 70);
    headerButton(aboutButton, 70);
    headerButton(settingsButton, 90);
    headerButton(checkButton, 140);
    header.removeFromRight(12);
    headerButton(redoButton, 64);
    headerButton(undoButton, 64);
    indicator.setBounds(header);

    area.removeFromTop(NordMetrics::gap);
    // The overlays cover everything below the header.
    checkView.setBounds(area);
    settingsPanel.setBounds(area);
    aboutPanel.setBounds(area);

    area.removeFromBottom(kMargin);
    area.removeFromLeft(kMargin);
    area.removeFromRight(kMargin);
    libraryPanel.setBounds(area.removeFromRight(kLibraryWidth));
    area.removeFromRight(kMargin);

    tabs.setBounds(area.removeFromTop(kTabsHeight));
    area.removeFromTop(NordMetrics::gap);
    selectorBar.setBounds(area.removeFromTop(SelectorBar::kPreferredHeight));

    auto warnings = area.removeFromTop(kWarningsHeight);
    warningLabel.setBounds(warnings);
    takeControlButton.setBounds(warnings.removeFromRight(120).withSizeKeepingCentre(120, NordMetrics::buttonHeight));
    warnings.removeFromRight(NordMetrics::gap);
    instanceWarning.setBounds(warnings.reduced(0, 2));

    slotView.setBounds(area);
    performanceView.setBounds(area);
    memoryPanel.setBounds(area);
}
