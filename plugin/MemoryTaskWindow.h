#pragma once

#include "nl2x/MemoryWriter.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

// What the memory task window shows in one phase of a Store or a Send (#44): reading the backup,
// the confirmation, the "no backup possible" consent, writing, and the outcome. Built from the
// memory writer's status by the functions below, so that the Librarian and nl2x_render_editor
// show the same pages.
struct MemoryTaskPage {
    juce::String title;
    // Selectable, as in SelectableAlertWindow.
    juce::String message;
    // No value: no progress bar. Below 0: a bar that moves without a known end.
    std::optional<double> progress;
    // The step under way, under the bar.
    juce::String progressText;
    // The first takes Return when there are two; the last takes Escape (a single one takes both).
    juce::StringArray buttons;
    // False greys out the last button (Cancel while the lit Slot is put back or the write read back).
    bool lastButtonEnabled = true;
};

namespace memoryTaskPages {

// One line saying what the writer is doing, with its progress ("Writing to Bank 3 (1 of 1). …").
juce::String stageText(const nl2x::MemoryTaskStatus& status);
// The writes are Store, Send Bank and the Librarian's Send; reads have no task window.
bool isWrite(nl2x::MemoryOperation operation);
// Reading the backup and the lit Slot before the confirmation (`confirmed` false), or writing,
// switching and putting back the lit Slot, reading back (`confirmed` true); with Cancel.
MemoryTaskPage working(const nl2x::MemoryTaskStatus& status, bool confirmed, bool bankReadingVerified,
                       const juce::String& taskTitle);
// The plan's question, panel steps, `locations` (one line per location, may be empty) and details;
// Overwrite and Cancel.
MemoryTaskPage confirmation(const nl2x::MemoryWritePlan& plan, const juce::String& locations);
// The second question of a plan without a backup.
MemoryTaskPage noBackup(const nl2x::MemoryWritePlan& plan);
// The task ended: finished, failed or stopped. `message` replaces the writer's message when set.
MemoryTaskPage outcome(const nl2x::MemoryTaskStatus& status, const juce::String& message = {});

} // namespace memoryTaskPages

// One window per Store or Send, open from the moment the task starts until the user closes it
// after the outcome: it goes through the phases in place (show() with each MemoryTaskPage), so
// the user never has to look for the progress or the messages at the bottom of the Librarian.
// Modal while shown, drawn like the editor's alert windows; its message can be selected and copied.
class MemoryTaskWindow final : public juce::TopLevelWindow {
public:
    explicit MemoryTaskWindow(juce::Component* associatedComponent);
    ~MemoryTaskWindow() override;

    // Puts the window on screen, modal (not done by the constructor, so that nl2x_render_editor can
    // paint one offscreen); close() takes it off again.
    void open();
    void close();
    // Shows `page`; `onButton` gets the index of the button clicked (also through Return and
    // Escape). The message keeps its selection while it does not change.
    void show(const MemoryTaskPage& page, std::function<void(int)> onButton);
    const MemoryTaskPage& page() const { return shown; }

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;

protected:
    void userTriedToCloseWindow() override;
    int getDesktopWindowStyleFlags() const override;
    float getDesktopScaleFactor() const override;
    void lookAndFeelChanged() override;

private:
    void press(int index);
    void layOut();
    juce::Rectangle<int> progressBounds() const;

    juce::Component::SafePointer<juce::Component> associated;
    float desktopScale = 1.0f;
    MemoryTaskPage shown;
    std::function<void(int)> buttonHandler;
    juce::TextEditor messageText;
    juce::Label progressLabel;
    // The first buttonCount are shown.
    std::vector<std::unique_ptr<juce::TextButton>> buttons;
    int buttonCount = 0;
    juce::ComponentDragger dragger;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MemoryTaskWindow)
};
