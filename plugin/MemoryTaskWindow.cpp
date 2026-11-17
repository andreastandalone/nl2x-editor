#include "MemoryTaskWindow.h"

#include "NordLookAndFeel.h"
#include "SelectableAlert.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr int kMargin = 20;
constexpr int kMessageWidth = 460;
constexpr int kMostMessageHeight = 420;
constexpr int kTitleHeight = 26;
constexpr int kGap = 12;
constexpr int kBarHeight = 8;
constexpr int kProgressLabelHeight = 20;
constexpr int kButtonGap = 16;
constexpr int kLeastButtonWidth = 100;
// The part of the bar that moves when the end is not known, and how long one sweep takes.
constexpr float kMovingPart = 0.25f;
constexpr double kSweepMs = 1600.0;

juce::String utf8(const std::string& text)
{
    return juce::String::fromUTF8(text.c_str());
}

juce::String bankText(int bank)
{
    return "Bank " + juce::String(bank);
}

} // namespace

namespace memoryTaskPages {

juce::String stageText(const nl2x::MemoryTaskStatus& status)
{
    const auto progress = " (" + juce::String(status.done) + " of " + juce::String(status.total) + ")";
    switch (status.stage) {
    case nl2x::MemoryStage::Idle:
    case nl2x::MemoryStage::Finished:
    case nl2x::MemoryStage::Failed:
        return {};
    case nl2x::MemoryStage::ReadingBackup:
        return "Backing up " + bankText(status.bank) + progress + ". Nothing is written yet.";
    case nl2x::MemoryStage::ReadingLitSlot:
        return "Asking the synth which Bank the lit Slot is on.";
    case nl2x::MemoryStage::AwaitingConfirmation:
        return "Waiting for your confirmation. Nothing is written yet.";
    case nl2x::MemoryStage::SwitchingBank:
        return "Switching the lit Slot to " + bankText(status.bank) + " for the write.";
    case nl2x::MemoryStage::Writing:
        return "Writing to " + bankText(status.bank) + progress + ".";
    case nl2x::MemoryStage::RestoringSlot:
        return "Putting the lit Slot back on its own Bank and Program.";
    case nl2x::MemoryStage::Verifying:
        return "Reading back what was written" + progress + ".";
    case nl2x::MemoryStage::ReadingBank:
        return "Reading " + bankText(status.bank) + " from the synth" + progress + ". Nothing is written.";
    }
    return {};
}

bool isWrite(nl2x::MemoryOperation operation)
{
    return operation != nl2x::MemoryOperation::ReadBank && operation != nl2x::MemoryOperation::ReadBanks;
}

MemoryTaskPage working(const nl2x::MemoryTaskStatus& status, bool confirmed, bool bankReadingVerified,
                       const juce::String& taskTitle)
{
    MemoryTaskPage page;
    page.title = taskTitle;
    if (! confirmed) {
        page.message = bankReadingVerified
                           ? juce::String::fromUTF8("Wait, don't play or touch the synth: reading ") + bankText(status.bank)
                                 + juce::String::fromUTF8(" from your synth for a backup\xE2\x80\xA6")
                           : juce::String::fromUTF8("Wait, don't play or touch the synth: asking it which Bank the lit "
                                                    "Slot is on\xE2\x80\xA6");
        page.message << "\n\nNothing is written yet. Then this window says what will happen on the synth, and nothing "
                        "is written before you click Overwrite.";
    } else {
        page.message = juce::String::fromUTF8("Writing\xE2\x80\xA6 don't play or touch the synth until this window "
                                              "says the write is done.");
    }
    page.progress = status.total > 0 ? static_cast<double>(status.done) / static_cast<double>(status.total) : -1.0;
    page.progressText = stageText(status);
    page.buttons = { "Cancel" };
    // The writer puts the lit Slot back before it stops, and a write being read back is done.
    page.lastButtonEnabled = status.stage != nl2x::MemoryStage::RestoringSlot
                             && status.stage != nl2x::MemoryStage::Verifying;
    return page;
}

MemoryTaskPage confirmation(const nl2x::MemoryWritePlan& plan, const juce::String& locations)
{
    MemoryTaskPage page;
    page.title = utf8(plan.question);
    // What happens on the synth, or what to do on its panel first: the lit Slot's Bank is where the
    // write goes.
    page.message = utf8(plan.panelSteps) + "\n\n";
    if (locations.isNotEmpty())
        page.message << locations << "\n";
    page.message << utf8(plan.details);
    page.buttons = { "Overwrite", "Cancel" };
    return page;
}

MemoryTaskPage noBackup(const nl2x::MemoryWritePlan& plan)
{
    MemoryTaskPage page;
    page.title = "No backup possible";
    page.message = utf8(plan.noBackupWarning);
    page.buttons = { "Write without a backup", "Cancel" };
    return page;
}

MemoryTaskPage outcome(const nl2x::MemoryTaskStatus& status, const juce::String& message)
{
    MemoryTaskPage page;
    if (status.stage == nl2x::MemoryStage::Finished)
        page.title = status.operation == nl2x::MemoryOperation::StoreProgram ? "Stored" : "Sent to the synth";
    else if (status.stage == nl2x::MemoryStage::Failed)
        page.title = "Writing to the synth failed";
    else
        page.title = "Stopped";
    auto text = message.isNotEmpty() ? message : utf8(status.message);
    if (status.backupFile && text.indexOf("ackup") < 0)
        text << " Backup: " << utf8(status.backupFile->string());
    page.message = text.replace(" Backup: ", "\n\nBackup: ");
    page.buttons = { "Close" };
    return page;
}

} // namespace memoryTaskPages

MemoryTaskWindow::MemoryTaskWindow(juce::Component* associatedComponent)
    : juce::TopLevelWindow("NL2X Editor", false), associated(associatedComponent),
      desktopScale(associatedComponent != nullptr ? juce::Component::getApproximateScaleFactorForComponent(associatedComponent)
                                                  : 1.0f)
{
    setAlwaysOnTop(juce::WindowUtils::areThereAnyAlwaysOnTopWindows());
    // The editor's look, as the alert windows have it.
    if (associatedComponent != nullptr)
        setLookAndFeel(&associatedComponent->getLookAndFeel());
    addAndMakeVisible(messageText);
    progressLabel.setFont(NordFonts::small());
    progressLabel.setColour(juce::Label::textColourId, NordPalette::textDim);
    progressLabel.setJustificationType(juce::Justification::centredLeft);
    progressLabel.setMinimumHorizontalScale(0.8f);
    addChildComponent(progressLabel);
    lookAndFeelChanged();
}

MemoryTaskWindow::~MemoryTaskWindow()
{
    setLookAndFeel(nullptr);
}

void MemoryTaskWindow::show(const MemoryTaskPage& page, std::function<void(int)> onButton)
{
    buttonHandler = std::move(onButton);
    const bool newText = page.title != shown.title || page.message != shown.message;
    const bool newButtons = page.buttons != shown.buttons || buttonCount == 0;
    const bool newBar = page.progress.has_value() != shown.progress.has_value();
    shown = page;
    setName(page.title);

    if (newText)
        setUpSelectableMessage(messageText, page.message, NordFonts::body(), findColour(juce::AlertWindow::textColourId),
                               kMessageWidth, kMostMessageHeight);
    progressLabel.setText(page.progressText, juce::dontSendNotification);
    progressLabel.setVisible(page.progress.has_value());

    if (newButtons) {
        // The buttons are kept, not deleted: a click on one may show the next page.
        auto& lookAndFeel = getLookAndFeel();
        const auto height = lookAndFeel.getAlertWindowButtonHeight();
        buttonCount = page.buttons.size();
        for (int i = 0; i < buttonCount; ++i) {
            if (i == static_cast<int>(buttons.size())) {
                buttons.push_back(std::make_unique<juce::TextButton>());
                buttons.back()->onClick = [this, i] { press(i); };
                addChildComponent(*buttons.back());
            }
            auto& button = *buttons[static_cast<std::size_t>(i)];
            button.setButtonText(page.buttons[i]);
            button.changeWidthToFitText(height);
            button.setSize(std::max(button.getWidth() + 16, kLeastButtonWidth), height);
            button.setVisible(true);
        }
        for (auto i = static_cast<std::size_t>(buttonCount); i < buttons.size(); ++i)
            buttons[i]->setVisible(false);
    }
    for (int i = 0; i < buttonCount; ++i)
        buttons[static_cast<std::size_t>(i)]->setEnabled(i < buttonCount - 1 || page.lastButtonEnabled);

    if (newText || newButtons || newBar)
        layOut();
    repaint();
}

void MemoryTaskWindow::open()
{
    addToDesktop(getDesktopWindowStyleFlags());
    setVisible(true);
    enterModalState(true);
}

void MemoryTaskWindow::close()
{
    exitModalState(0);
    setVisible(false);
}

void MemoryTaskWindow::press(int index)
{
    if (index < 0 || index >= buttonCount || ! buttons[static_cast<std::size_t>(index)]->isEnabled())
        return;
    // The handler may show another page, which replaces the buttons: keep it until it returns.
    const auto handler = buttonHandler;
    if (handler)
        handler(index);
}

void MemoryTaskWindow::layOut()
{
    int height = kMargin + kTitleHeight + kGap + messageText.getHeight();
    if (shown.progress)
        height += kGap + kBarHeight + 4 + kProgressLabelHeight;
    if (buttonCount > 0)
        height += kGap + 8 + buttons.front()->getHeight();
    height += kMargin;
    const int width = kMessageWidth + 2 * kMargin;

    if (! isVisible())
        centreAroundComponent(associated, width, height);
    else
        setBounds(getBounds().withSizeKeepingCentre(width, height));
    resized();
}

juce::Rectangle<int> MemoryTaskWindow::progressBounds() const
{
    return { kMargin, messageText.getBottom() + kGap, kMessageWidth, kBarHeight };
}

void MemoryTaskWindow::resized()
{
    messageText.setTopLeftPosition(kMargin, kMargin + kTitleHeight + kGap);
    const auto bar = progressBounds();
    progressLabel.setBounds(kMargin, bar.getBottom() + 4, kMessageWidth, kProgressLabelHeight);

    int total = -kButtonGap;
    for (int i = 0; i < buttonCount; ++i)
        total += buttons[static_cast<std::size_t>(i)]->getWidth() + kButtonGap;
    int x = (getWidth() - total) / 2;
    for (int i = 0; i < buttonCount; ++i) {
        auto& button = *buttons[static_cast<std::size_t>(i)];
        button.setTopLeftPosition(x, getHeight() - kMargin - button.getHeight());
        x += button.getWidth() + kButtonGap;
    }
}

void MemoryTaskWindow::paint(juce::Graphics& g)
{
    constexpr float corner = 4.0f;
    g.setColour(findColour(juce::AlertWindow::outlineColourId));
    g.drawRoundedRectangle(getLocalBounds().toFloat(), corner, 2.0f);
    g.setColour(findColour(juce::AlertWindow::backgroundColourId));
    g.fillRoundedRectangle(getLocalBounds().reduced(1).toFloat(), corner);

    g.setColour(findColour(juce::AlertWindow::textColourId));
    g.setFont(getLookAndFeel().getAlertWindowTitleFont());
    g.drawFittedText(shown.title, juce::Rectangle<int>(kMargin, kMargin, kMessageWidth, kTitleHeight),
                     juce::Justification::centred, 1);

    if (shown.progress) {
        const auto bar = progressBounds().toFloat();
        const float radius = bar.getHeight() / 2.0f;
        g.setColour(NordPalette::knobTrack);
        g.fillRoundedRectangle(bar, radius);
        g.setColour(NordPalette::valueArc);
        const double value = *shown.progress;
        if (value >= 0.0) {
            g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * static_cast<float>(std::clamp(value, 0.0, 1.0))), radius);
        } else {
            // No known end: a part of the bar sweeps across it (show() repaints on every refresh).
            const auto phase = std::fmod(juce::Time::getMillisecondCounterHiRes(), kSweepMs) / kSweepMs;
            const float width = bar.getWidth() * kMovingPart;
            const float x = bar.getX() + static_cast<float>(phase) * (bar.getWidth() - width);
            g.fillRoundedRectangle(bar.withX(x).withWidth(width), radius);
        }
    }
}

bool MemoryTaskWindow::keyPressed(const juce::KeyPress& key)
{
    if (buttonCount == 0)
        return false;
    if (key == juce::KeyPress::escapeKey) {
        press(buttonCount - 1);
        return true;
    }
    if (key == juce::KeyPress::returnKey) {
        press(0);
        return true;
    }
    return false;
}

void MemoryTaskWindow::mouseDown(const juce::MouseEvent& event)
{
    dragger.startDraggingComponent(this, event);
}

void MemoryTaskWindow::mouseDrag(const juce::MouseEvent& event)
{
    dragger.dragComponent(this, event, nullptr);
}

void MemoryTaskWindow::userTriedToCloseWindow()
{
    if (buttonCount > 0)
        press(buttonCount - 1);
}

int MemoryTaskWindow::getDesktopWindowStyleFlags() const
{
    return getLookAndFeel().getAlertBoxWindowFlags();
}

float MemoryTaskWindow::getDesktopScaleFactor() const
{
    return desktopScale * juce::Desktop::getInstance().getGlobalScaleFactor();
}

void MemoryTaskWindow::lookAndFeelChanged()
{
    const int flags = getLookAndFeel().getAlertBoxWindowFlags();
    setUsingNativeTitleBar((flags & juce::ComponentPeer::windowHasTitleBar) != 0);
    setDropShadowEnabled(isOpaque() && (flags & juce::ComponentPeer::windowHasDropShadow) != 0);
    if (isOnDesktop())
        addToDesktop(flags);
}
