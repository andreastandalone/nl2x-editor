#include "MemoryPanel.h"

#include "nl2x/HardwareAssumptions.h"

#include <filesystem>

namespace {

constexpr int kRefreshIntervalMs = 100;

juce::String utf8(const std::string& text)
{
    return juce::String::fromUTF8(text.c_str());
}

std::filesystem::path toPath(const juce::File& file)
{
    return std::filesystem::path(file.getFullPathName().toStdString());
}

// Where every backup goes (spec #1, user story 62), whatever the Library folder is.
juce::File backupFolder()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("Nord Lead 2X")
        .getChildFile("Backups");
}

nl2x::WallClockTime wallClockNow()
{
    const auto now = juce::Time::getCurrentTime();
    return { now.getYear(), now.getMonth() + 1, now.getDayOfMonth(), now.getHours(), now.getMinutes(),
             now.getSeconds() };
}

juce::String stageText(const nl2x::MemoryTaskStatus& status)
{
    const auto progress = " (" + juce::String(status.done) + " of " + juce::String(status.total) + ")";
    switch (status.stage) {
    case nl2x::MemoryStage::Idle:
    case nl2x::MemoryStage::Finished:
    case nl2x::MemoryStage::Failed:
        return {};
    case nl2x::MemoryStage::ReadingBackup:
        return "Backing up Bank " + juce::String(status.bank) + progress + ". Nothing is written yet.";
    case nl2x::MemoryStage::AwaitingConfirmation:
        return "Waiting for your confirmation. Nothing is written yet.";
    case nl2x::MemoryStage::CheckingSelectedBank:
        return "Checking which Bank is selected on the synth.";
    case nl2x::MemoryStage::Writing:
        return "Writing to Bank " + juce::String(status.bank) + progress + ". Do not change the Bank on the synth.";
    case nl2x::MemoryStage::Verifying:
        return "Reading back what was written" + progress + ".";
    case nl2x::MemoryStage::ReadingBank:
        return "Reading Bank " + juce::String(status.bank) + progress + ".";
    }
    return {};
}

} // namespace

MemoryPanel::MemoryPanel(NL2XEditorProcessor& owner, LibraryPanel& library, std::function<nl2x::Slot()> slotChoice)
    : processor(owner), libraryPanel(library), selectedSlot(std::move(slotChoice))
{
    title.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    lockLabel.setJustificationType(juce::Justification::topLeft);
    lockLabel.setText(processor.synthMemory().bankReadingVerified()
                          ? "Before every overwrite, the Bank is backed up automatically into "
                                + backupFolder().getFullPathName() + "."
                          : "Reading Banks from the synth is not verified on hardware yet, so no backup can be made "
                            "before a write: every write asks for a separate \"no backup\" confirmation, and "
                            "reading a Bank is experimental.",
                      juce::dontSendNotification);

    for (int bank = nl2x::hardware::kFirstRamProgramBank; bank <= nl2x::hardware::kLastRamProgramBank; ++bank)
        bankChoice.addItem("Bank " + juce::String(bank), bank + 1);
    bankChoice.setSelectedId(1, juce::dontSendNotification);
    bankChoice.setTooltip("The RAM Bank to write or read, numbered as the synth shows it");
    for (int program = 1; program <= nl2x::hardware::kProgramsPerRamBank; ++program)
        programChoice.addItem(juce::String(program), program);
    programChoice.setSelectedId(1, juce::dontSendNotification);

    storeButton.onClick = [this] { store(); };
    storeButton.setTooltip("Overwrite one Program in the chosen Bank with the selected Slot's Program (asks first)");
    readButton.onClick = [this] { readBank(); };
    readButton.setTooltip("Read Programs 1-99 of the chosen Bank into a .syx file in the Library");
    sendButton.onClick = [this] { sendBank(); };
    sendButton.setTooltip("Overwrite the chosen Bank with the Programs of the Bank file selected in the Library "
                          "(asks first)");
    cancelButton.onClick = [this] { processor.synthMemory().cancel(); };

    progressLabel.setJustificationType(juce::Justification::topLeft);
    messageLabel.setJustificationType(juce::Justification::topLeft);

    for (auto* child : std::initializer_list<juce::Component*> { &title, &lockLabel, &bankLabel, &bankChoice,
                                                                 &programLabel, &programChoice, &storeButton,
                                                                 &readButton, &sendButton, &cancelButton,
                                                                 &progressLabel, &messageLabel })
        addAndMakeVisible(child);

    timerCallback();
    startTimer(kRefreshIntervalMs);
}

MemoryPanel::~MemoryPanel()
{
    stopTimer();
}

void MemoryPanel::resized()
{
    auto area = getLocalBounds().reduced(8);
    title.setBounds(area.removeFromTop(24));
    lockLabel.setBounds(area.removeFromTop(48));
    area.removeFromTop(8);

    auto row = area.removeFromTop(26);
    bankLabel.setBounds(row.removeFromLeft(50));
    bankChoice.setBounds(row.removeFromLeft(100));
    row.removeFromLeft(16);
    programLabel.setBounds(row.removeFromLeft(70));
    programChoice.setBounds(row.removeFromLeft(80));
    area.removeFromTop(12);

    for (auto* button : { &storeButton, &readButton, &sendButton }) {
        button->setBounds(area.removeFromTop(28).removeFromLeft(320));
        area.removeFromTop(6);
    }
    area.removeFromTop(6);
    auto progressRow = area.removeFromTop(28);
    cancelButton.setBounds(progressRow.removeFromRight(90));
    progressRow.removeFromRight(8);
    progressLabel.setBounds(progressRow);
    area.removeFromTop(6);
    messageLabel.setBounds(area.removeFromTop(120));
}

int MemoryPanel::chosenBank() const
{
    return bankChoice.getSelectedId() - 1;
}

void MemoryPanel::timerCallback()
{
    const auto slot = selectedSlot();
    storeButton.setButtonText(juce::String::fromUTF8("Store Slot ") + juce::String::charToString(nl2x::slotLetter(slot))
                              + juce::String::fromUTF8(" to Bank/Program\xE2\x80\xA6"));

    auto& memory = processor.synthMemory();
    const auto& status = memory.status();
    const bool idle = ! memory.busy();
    const bool controls = processor.controlsSynth();
    for (auto* button : { &storeButton, &readButton, &sendButton })
        button->setEnabled(idle && controls);
    cancelButton.setEnabled(! idle);

    progressLabel.setText(stageText(status), juce::dontSendNotification);
    juce::String message = refusalText.isNotEmpty() ? refusalText : utf8(status.message);
    if (status.backupFile && message.indexOf("Backup") < 0)
        message << "\nBackup: " << utf8(status.backupFile->string());
    messageLabel.setText(message, juce::dontSendNotification);

    if (status.stage == nl2x::MemoryStage::AwaitingConfirmation && status.plan && status.plan->token != askedToken)
        askForConfirmation(*status.plan);
}

void MemoryPanel::showRefusal(nl2x::MemoryRefusal refusal)
{
    refusalText = refusal == nl2x::MemoryRefusal::None ? juce::String() : utf8(nl2x::memoryRefusalText(refusal));
    timerCallback();
}

void MemoryPanel::store()
{
    const auto refusal = processor.synthMemory().prepareStoreProgram(
        selectedSlot(), chosenBank(), programChoice.getSelectedId(), toPath(backupFolder()), wallClockNow(),
        NL2XEditorProcessor::engineTimeMs());
    showRefusal(refusal);
}

void MemoryPanel::sendBank()
{
    const auto* file = libraryPanel.selectedBankFile();
    if (file == nullptr) {
        refusalText = "Select a Bank file in the Library first (click its name).";
        timerCallback();
        return;
    }
    const auto refusal = processor.synthMemory().prepareBankSend(*file, chosenBank(), toPath(backupFolder()),
                                                                 wallClockNow(), NL2XEditorProcessor::engineTimeMs());
    showRefusal(refusal);
}

void MemoryPanel::readBank()
{
    const auto bank = chosenBank();
    const auto folder = libraryPanel.folder();
    if (! folder.isDirectory() && ! folder.createDirectory()) {
        refusalText = "Cannot create the Library folder " + folder.getFullPathName() + ".";
        timerCallback();
        return;
    }
    const auto suggested = nl2x::uniqueLibraryPath(
        toPath(folder), "Bank " + std::to_string(bank) + " " + juce::Time::getCurrentTime().formatted("%Y-%m-%d").toStdString());
    chooser = std::make_unique<juce::FileChooser>("Read Bank " + juce::String(bank) + " into a .syx file",
                                                  juce::File(utf8(suggested.string())), "*.syx");
    const auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                       | juce::FileBrowserComponent::warnAboutOverwriting;
    chooser->launchAsync(flags, [safe = juce::Component::SafePointer<MemoryPanel>(this), bank](const juce::FileChooser& chosen) {
        if (safe == nullptr)
            return;
        const auto picked = chosen.getResult();
        if (picked == juce::File())
            return;
        // The dialog asked before replacing the file it returned, not one reached by adding ".syx".
        const auto target = picked.hasFileExtension(".syx") ? picked : picked.withFileExtension(".syx");
        const auto replace = target == picked ? nl2x::ReplaceExisting::Yes : nl2x::ReplaceExisting::No;
        safe->showRefusal(safe->processor.synthMemory().readBank(bank, toPath(target), replace,
                                                                 NL2XEditorProcessor::engineTimeMs()));
    });
}

void MemoryPanel::askForConfirmation(const nl2x::MemoryWritePlan& plan)
{
    askedToken = plan.token;
    refusalText.clear();
    auto details = utf8(plan.details);
    const auto& status = processor.synthMemory().status();
    if (! status.message.empty())
        details = utf8(status.message) + "\n\n" + details;

    const auto options = juce::MessageBoxOptions()
                             .withIconType(juce::MessageBoxIconType::WarningIcon)
                             .withTitle(utf8(plan.question))
                             .withMessage(details)
                             .withButton("Overwrite")
                             .withButton("Cancel")
                             .withAssociatedComponent(this);
    juce::AlertWindow::showAsync(options, [safe = juce::Component::SafePointer<MemoryPanel>(this), plan](int result) {
        if (safe == nullptr)
            return;
        if (result != 1) {
            safe->processor.synthMemory().cancel();
            return;
        }
        if (plan.needsNoBackupConsent)
            safe->askForNoBackupConsent(plan);
        else
            safe->confirm(plan.token, nl2x::NoBackupConsent::NotGiven);
    });
}

void MemoryPanel::askForNoBackupConsent(const nl2x::MemoryWritePlan& plan)
{
    const auto options = juce::MessageBoxOptions()
                             .withIconType(juce::MessageBoxIconType::WarningIcon)
                             .withTitle("No backup possible")
                             .withMessage(utf8(plan.noBackupWarning))
                             .withButton("Write without a backup")
                             .withButton("Cancel")
                             .withAssociatedComponent(this);
    juce::AlertWindow::showAsync(options, [safe = juce::Component::SafePointer<MemoryPanel>(this), plan](int result) {
        if (safe == nullptr)
            return;
        if (result == 1)
            safe->confirm(plan.token, nl2x::NoBackupConsent::Given);
        else
            safe->processor.synthMemory().cancel();
    });
}

void MemoryPanel::confirm(std::uint64_t token, nl2x::NoBackupConsent consent)
{
    const auto result = processor.synthMemory().confirm(token, consent, NL2XEditorProcessor::engineTimeMs());
    if (result == nl2x::MemoryConfirmResult::RefusedNoSuchPlan)
        refusalText = "That question is no longer current: nothing was written.";
    timerCallback();
}
