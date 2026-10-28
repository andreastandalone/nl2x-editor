#include "ProbeWindow.h"

#include "nl2x/HardwareAssumptions.h"

using nl2x::probe::ScriptId;

namespace {

// Pacing needs millisecond steps; the probe's clock ticks this often.
constexpr int kTimerIntervalMs = 1;
// Appending to the log view is slow; batch it.
constexpr double kLogFlushIntervalMs = 50.0;

double nowMs()
{
    return juce::Time::getMillisecondCounterHiRes();
}

nl2x::TimeMs engineTime(double timeMs)
{
    return static_cast<nl2x::TimeMs>(timeMs);
}

juce::String fromUtf8(std::string_view text)
{
    return juce::String::fromUTF8(text.data(), static_cast<int>(text.size()));
}

// An integer typed in a field, if it lies in [minValue, maxValue].
std::optional<int> parseInRange(const juce::String& text, int minValue, int maxValue)
{
    const auto trimmed = text.trim();
    if (trimmed.isEmpty() || ! trimmed.containsOnly("0123456789"))
        return std::nullopt;
    const auto value = trimmed.getIntValue();
    if (value < minValue || value > maxValue)
        return std::nullopt;
    return value;
}

juce::String timestampForFileName()
{
    return juce::Time::getCurrentTime().formatted("%Y-%m-%d-%H%M%S");
}

} // namespace

ProbeComponent::ProbeComponent(NL2XEditorProcessor& owner)
    : processor(owner),
      log(nowMs()),
      session(output, [this](std::string_view line) { note(fromUtf8(line)); }),
      portSelector(owner.midiPorts()),
      shownGlobalChannel(session.globalChannel())
{
    addAndMakeVisible(portSelector);

    setUpField(globalChannelField, "Global Channel (1–16)",
               juce::String(session.globalChannel() + nl2x::hardware::kShownGlobalChannelOfDev0));
    setUpField(sweepCcField, "CC sweep: CC number", "35");
    setUpField(sweepChannelField, "CC sweep: channel (1–16)", "1");
    setUpField(bankRequestTypeField, "Bank request type (from step 5)", {});
    setUpField(bankDumpTypeField, "Bank dump type (1–10)", "4");

    for (const auto& info : nl2x::probe::scripts()) {
        auto* button = scriptButtons.add(new juce::TextButton(fromUtf8(info.name)));
        button->setTooltip(fromUtf8(info.description));
        if (info.writesToMemory)
            button->setColour(juce::TextButton::buttonColourId, juce::Colour(0xffa0282d));
        button->onClick = [this, id = info.id] { runScript(id); };
        addAndMakeVisible(button);
    }

    stopButton.onClick = [this] { session.stop(engineTime(nowMs())); };
    addAndMakeVisible(stopButton);
    addAndMakeVisible(statusLabel);

    rawEntry.setTextToShowWhenEmpty("Raw message in hex, e.g. F0 33 0F 04 0E 00 F7", juce::Colours::grey);
    rawEntry.onReturnKey = [this] { sendRawMessage(); };
    sendButton.onClick = [this] { sendRawMessage(); };
    addAndMakeVisible(rawEntry);
    addAndMakeVisible(sendButton);

    logView.setMultiLine(true, false);
    logView.setReadOnly(true);
    logView.setScrollbarsShown(true);
    logView.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
    addAndMakeVisible(logView);

    clearButton.onClick = [this] {
        log.clear(nowMs());
        pendingLogText.clear();
        logView.clear();
    };
    saveButton.onClick = [this] { saveLog(); };
    addAndMakeVisible(clearButton);
    addAndMakeVisible(saveButton);

    session.onBankBackupCaptured = [this](const nl2x::probe::BankBackup& backup) { saveBankBackup(backup); };

    processor.setMidiMonitor(this);
    startTimer(kTimerIntervalMs);
    setSize(1200, 820);
}

ProbeComponent::~ProbeComponent()
{
    stopTimer();
    processor.setMidiMonitor(nullptr);
    processor.setEngineRequestsSuspended(false);
}

void ProbeComponent::setUpField(Field& field, const juce::String& labelText, const juce::String& initialText)
{
    field.label.setText(labelText, juce::dontSendNotification);
    field.editor.setText(initialText, false);
    field.editor.setInputRestrictions(3, "0123456789");
    addAndMakeVisible(field.label);
    addAndMakeVisible(field.editor);
}

void ProbeComponent::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void ProbeComponent::resized()
{
    auto area = getLocalBounds().reduced(12);

    auto left = area.removeFromLeft(470);
    area.removeFromLeft(12);

    portSelector.setBounds(left.removeFromTop(MidiPortSelector::kPreferredHeight));
    left.removeFromTop(8);
    for (auto* field : { &globalChannelField, &sweepCcField, &sweepChannelField, &bankRequestTypeField,
                         &bankDumpTypeField }) {
        auto row = left.removeFromTop(26);
        field->label.setBounds(row.removeFromLeft(260));
        field->editor.setBounds(row.removeFromLeft(60));
        left.removeFromTop(4);
    }
    left.removeFromTop(8);

    auto bottom = left.removeFromBottom(28);
    stopButton.setBounds(bottom.removeFromLeft(100));
    bottom.removeFromLeft(8);
    statusLabel.setBounds(bottom);
    left.removeFromBottom(8);

    const auto rows = (scriptButtons.size() + 1) / 2;
    const auto rowHeight = juce::jmin(40, left.getHeight() / juce::jmax(1, rows));
    const auto columnWidth = (left.getWidth() - 6) / 2;
    for (int i = 0; i < scriptButtons.size(); ++i) {
        const auto column = i % 2;
        const auto row = i / 2;
        scriptButtons[i]->setBounds(left.getX() + column * (columnWidth + 6), left.getY() + row * rowHeight,
                                    columnWidth, rowHeight - 4);
    }

    auto rawRow = area.removeFromTop(28);
    sendButton.setBounds(rawRow.removeFromRight(80));
    rawRow.removeFromRight(6);
    rawEntry.setBounds(rawRow);
    area.removeFromTop(8);

    auto buttons = area.removeFromBottom(28);
    saveButton.setBounds(buttons.removeFromRight(100));
    buttons.removeFromRight(6);
    clearButton.setBounds(buttons.removeFromRight(100));
    area.removeFromBottom(8);
    logView.setBounds(area);
}

void ProbeComponent::midiFromSynth(std::span<const std::uint8_t> message, double timeMs)
{
    appendLine(log.addMidi(timeMs, nl2x::probe::Direction::In, message));
    session.handleIncomingMidi(message, engineTime(nowMs()));
}

void ProbeComponent::midiToSynth(std::span<const std::uint8_t> message, double timeMs)
{
    appendLine(log.addMidi(timeMs, nl2x::probe::Direction::Out, message));
}

void ProbeComponent::timerCallback()
{
    processor.deliverIncomingMidi();
    session.advanceTime(engineTime(nowMs()));

    if (session.globalChannel() != shownGlobalChannel && ! globalChannelField.editor.hasKeyboardFocus(true)) {
        shownGlobalChannel = session.globalChannel();
        globalChannelField.editor.setText(
            juce::String(shownGlobalChannel + nl2x::hardware::kShownGlobalChannelOfDev0), false);
    }

    // The engine stays out of the way while a script runs: it could take a reply to the script's
    // request for its own, and its requests would disturb the script.
    processor.setEngineRequestsSuspended(session.isRunning());

    const auto status = session.isRunning() ? juce::String("Running…") : juce::String("Idle");
    if (statusLabel.getText() != status)
        statusLabel.setText(status, juce::dontSendNotification);

    if (pendingLogText.isNotEmpty() && nowMs() - lastFlushMs >= kLogFlushIntervalMs)
        flushLog();
}

bool ProbeComponent::applyGlobalChannel()
{
    const auto shown = parseInRange(globalChannelField.editor.getText(), 1, 16);
    if (! shown) {
        note("The Global Channel must be 1–16");
        return false;
    }
    const auto channel = static_cast<nl2x::GlobalChannel>(*shown - nl2x::hardware::kShownGlobalChannelOfDev0);
    session.setGlobalChannel(channel);
    shownGlobalChannel = channel;
    return true;
}

std::optional<nl2x::probe::ScriptOptions> ProbeComponent::readOptions()
{
    nl2x::probe::ScriptOptions options;

    const auto cc = parseInRange(sweepCcField.editor.getText(), 0, 127);
    const auto channel = parseInRange(sweepChannelField.editor.getText(), 1, 16);
    const auto dumpType = parseInRange(bankDumpTypeField.editor.getText(), 1, 10);
    if (! cc || ! channel || ! dumpType) {
        note("Check the fields: CC 0–127, CC sweep channel 1–16, Bank dump type 1–10");
        return std::nullopt;
    }
    options.sweepCc = static_cast<std::uint8_t>(*cc);
    options.sweepChannel = static_cast<nl2x::MidiChannel>(*channel - 1);
    options.bankDumpType = static_cast<std::uint8_t>(*dumpType);

    if (bankRequestTypeField.editor.getText().trim().isNotEmpty()) {
        const auto requestType = parseInRange(bankRequestTypeField.editor.getText(), 0, 127);
        if (! requestType) {
            note("The Bank request type must be 0–127, or empty");
            return std::nullopt;
        }
        options.bankRequestType = static_cast<std::uint8_t>(*requestType);
    }
    return options;
}

void ProbeComponent::runScript(ScriptId id)
{
    if (! applyGlobalChannel())
        return;
    const auto options = readOptions();
    if (! options)
        return;
    if (processor.midiPorts().outputIdentifier().isEmpty())
        note("No MIDI output port is open: nothing reaches the synth");

    const auto& info = nl2x::probe::scriptInfo(id);
    if (! info.writesToMemory) {
        startScript(id, *options);
        return;
    }

    const auto message = fromUtf8(info.description)
                         + "\n\nSelect the Bank you backed up on the synth's panel before you go on.";
    juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                     .withIconType(juce::MessageBoxIconType::WarningIcon)
                                     .withTitle("Write to the synth's memory?")
                                     .withMessage(message)
                                     .withButton("Write")
                                     .withButton("Cancel")
                                     .withAssociatedComponent(this),
                                 [safeThis = juce::Component::SafePointer<ProbeComponent>(this), id,
                                  chosenOptions = *options](int result) {
                                     // 1 = the first button ("Write"); 0 = Cancel or Escape.
                                     if (safeThis != nullptr && result == 1)
                                         safeThis->startScript(id, chosenOptions);
                                 });
}

void ProbeComponent::startScript(ScriptId id, const nl2x::probe::ScriptOptions& options)
{
    processor.setEngineRequestsSuspended(true);
    session.start(id, options, engineTime(nowMs()));
}

void ProbeComponent::sendRawMessage()
{
    const auto parsed = nl2x::probe::parseRawMessage(rawEntry.getText().toStdString());
    if (! parsed.error.empty()) {
        note("Raw message not sent: " + fromUtf8(parsed.error));
        return;
    }
    processor.sendToSynth(parsed.bytes);
}

void ProbeComponent::saveLog()
{
    flushLog();
    const auto defaultFile = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                 .getChildFile("nl2x-probe-" + timestampForFileName() + ".txt");
    chooser = std::make_unique<juce::FileChooser>("Save the probe log", defaultFile, "*.txt");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                             | juce::FileBrowserComponent::warnAboutOverwriting,
                         [this](const juce::FileChooser& fileChooser) {
                             const auto file = fileChooser.getResult();
                             if (file == juce::File())
                                 return;
                             if (file.replaceWithText(fromUtf8(log.text())))
                                 note("Log saved to " + file.getFullPathName());
                             else
                                 note("Could not save the log to " + file.getFullPathName());
                         });
}

void ProbeComponent::saveBankBackup(const nl2x::probe::BankBackup& backup)
{
    // Saved at once, so the backup survives the app; the replies are kept exactly as received.
    const auto folder = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                            .getChildFile("Nord Lead 2X")
                            .getChildFile("Backups");
    const auto file = folder.getChildFile("probe-bank-backup-" + timestampForFileName() + "-request-type-"
                                          + juce::String(backup.requestType) + ".syx");

    juce::MemoryBlock data;
    for (const auto& reply : backup.replies)
        data.append(reply.data(), reply.size());

    if (folder.createDirectory() && file.replaceWithData(data.getData(), data.getSize()))
        note("Bank backup saved to " + file.getFullPathName());
    else
        note("Could not save the Bank backup to " + file.getFullPathName() + "; it is kept in memory only");
}

void ProbeComponent::note(const juce::String& text)
{
    appendLine(log.addNote(nowMs(), text.toStdString()));
}

void ProbeComponent::appendLine(const std::string& line)
{
    pendingLogText << fromUtf8(line) << "\n";
}

void ProbeComponent::flushLog()
{
    lastFlushMs = nowMs();
    if (pendingLogText.isEmpty())
        return;
    logView.moveCaretToEnd();
    logView.insertTextAtCaret(pendingLogText);
    pendingLogText.clear();
}

ProbeWindow::ProbeWindow(NL2XEditorProcessor& owner)
    : DocumentWindow("NL2X Editor Probe", juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(
                                              juce::ResizableWindow::backgroundColourId),
                     juce::DocumentWindow::allButtons)
{
    setUsingNativeTitleBar(true);
    setContentOwned(new ProbeComponent(owner), true);
    setResizable(true, false);
    centreWithSize(getWidth(), getHeight());
}
