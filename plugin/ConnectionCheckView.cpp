#include "ConnectionCheckView.h"

namespace {

constexpr int kRefreshIntervalMs = 200;

juce::String outcomeText(nl2x::ConnectionCheckOutcome outcome)
{
    switch (outcome) {
    case nl2x::ConnectionCheckOutcome::Pending: return "Waiting";
    case nl2x::ConnectionCheckOutcome::Running: return "Testing...";
    case nl2x::ConnectionCheckOutcome::Passed: return "OK";
    case nl2x::ConnectionCheckOutcome::Failed: return "Problem";
    case nl2x::ConnectionCheckOutcome::NotTested: return "Not tested";
    }
    return {};
}

juce::Colour outcomeColour(nl2x::ConnectionCheckOutcome outcome)
{
    switch (outcome) {
    case nl2x::ConnectionCheckOutcome::Passed: return juce::Colour(0xff3cb043);
    case nl2x::ConnectionCheckOutcome::Failed: return juce::Colour(0xffd83a2e);
    case nl2x::ConnectionCheckOutcome::Running: return juce::Colour(0xffe0a020);
    case nl2x::ConnectionCheckOutcome::Pending:
    case nl2x::ConnectionCheckOutcome::NotTested: return juce::Colour(0xff8a8a8a);
    }
    return juce::Colours::grey;
}

juce::String fromUtf8(const std::string& text)
{
    return juce::String::fromUTF8(text.c_str(), static_cast<int>(text.size()));
}

} // namespace

ConnectionCheckView::ConnectionCheckView(NL2XEditorProcessor& owner)
    : processor(owner)
{
    title.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    intro.setText("Tests the MIDI link with the synth in both directions. To test CCs, the editor moves the Filter "
                  "Cutoff of one Slot briefly and puts it back.",
                  juce::dontSendNotification);
    intro.setJustificationType(juce::Justification::topLeft);

    results.setMultiLine(true);
    results.setReadOnly(true);
    results.setCaretVisible(false);
    results.setScrollbarsShown(true);

    prompt.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    prompt.setColour(juce::Label::backgroundColourId, juce::Colour(0xffe0a020).withAlpha(0.25f));
    prompt.setJustificationType(juce::Justification::centredLeft);

    skipButton.setTooltip("Go on without this step; its test is reported as not tested");
    skipButton.onClick = [this] { processor.skipConnectionCheckStep(); };
    runButton.onClick = [this] {
        processor.closeConnectionCheck();
        processor.openConnectionCheck();
        refresh();
    };
    closeButton.onClick = [this] { processor.closeConnectionCheck(); };

    for (auto* child : std::initializer_list<juce::Component*> { &title, &intro, &results, &runButton, &closeButton })
        addAndMakeVisible(child);
    addChildComponent(prompt);
    addChildComponent(skipButton);

    refresh();
    startTimer(kRefreshIntervalMs);
}

ConnectionCheckView::~ConnectionCheckView()
{
    stopTimer();
}

void ConnectionCheckView::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId).brighter(0.08f));
    g.setColour(getLookAndFeel().findColour(juce::Label::textColourId).withAlpha(0.3f));
    g.drawRect(getLocalBounds());
}

void ConnectionCheckView::resized()
{
    auto area = getLocalBounds().reduced(12);
    title.setBounds(area.removeFromTop(26));
    intro.setBounds(area.removeFromTop(40));

    auto buttons = area.removeFromBottom(28);
    closeButton.setBounds(buttons.removeFromRight(90));
    buttons.removeFromRight(8);
    runButton.setBounds(buttons.removeFromRight(170));
    area.removeFromBottom(8);

    if (prompt.isVisible()) {
        auto promptArea = area.removeFromTop(56);
        skipButton.setBounds(promptArea.removeFromRight(80).withSizeKeepingCentre(80, 28));
        promptArea.removeFromRight(8);
        prompt.setBounds(promptArea);
        area.removeFromTop(8);
    }
    results.setBounds(area);
}

void ConnectionCheckView::timerCallback()
{
    refresh();
}

void ConnectionCheckView::refresh()
{
    const auto& report = processor.synth().connectionCheck();
    const bool waiting = report.state == nl2x::ConnectionCheckState::WaitingForUser;

    juce::String promptText;
    if (waiting) {
        const auto left = juce::jmax<nl2x::TimeMs>(
            0, (report.userStepDeadline - NL2XEditorProcessor::engineTimeMs() + 999) / 1000);
        promptText = fromUtf8(report.userPrompt) + "  (" + juce::String(left) + " s)";
    }
    if (promptText != shownPrompt || prompt.isVisible() != waiting) {
        shownPrompt = promptText;
        prompt.setText(promptText, juce::dontSendNotification);
        prompt.setVisible(waiting);
        skipButton.setVisible(waiting);
        resized();
    }

    const auto running = report.state == nl2x::ConnectionCheckState::Running || waiting;
    runButton.setEnabled(! running && processor.controlsSynth());

    if (processor.synth().stateRevision() != shownRevision) {
        shownRevision = processor.synth().stateRevision();
        showResults();
    }
}

void ConnectionCheckView::showResults()
{
    const auto& report = processor.synth().connectionCheck();
    const auto text = getLookAndFeel().findColour(juce::Label::textColourId);
    results.clear();

    const auto add = [this](const juce::String& line, juce::Colour colour, float size, bool bold) {
        results.setFont(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain));
        results.setColour(juce::TextEditor::textColourId, colour);
        results.insertTextAtCaret(line);
    };

    if (report.state == nl2x::ConnectionCheckState::Idle) {
        juce::String why;
        if (! processor.controlsSynth())
            why = "Another instance of NL2X Editor controls the synth, so this one cannot test it.";
        else if (processor.midiPorts().inputIdentifier().isEmpty() || processor.midiPorts().outputIdentifier().isEmpty())
            why = "Choose the MIDI Input (from the synth) and the MIDI Output (to the synth) in the settings, then "
                  "run the check.";
        else
            why = "Press \"Run the check again\" to test the connection.";
        add(why, text, 15.0f, false);
        return;
    }

    for (const auto& result : report.results) {
        add(outcomeText(result.outcome) + "   ", outcomeColour(result.outcome), 15.0f, true);
        add(fromUtf8(result.title) + "\n", text, 15.0f, true);
        if (! result.explanation.empty())
            add(fromUtf8(result.explanation) + "\n", text, 14.0f, false);
        if (! result.fix.empty())
            add("What to do: " + fromUtf8(result.fix) + "\n", text, 14.0f, false);
        add("\n", text, 8.0f, false);
    }

    if (report.state == nl2x::ConnectionCheckState::Finished)
        add(report.allPassed() ? "Everything works." : "Fix the problems above, then run the check again.", text,
            15.0f, true);

    results.moveCaretToTop(false);
}
