#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

// An AlertWindow whose message can be selected and copied (drag, Cmd+A, Cmd+C, or the context
// menu's Copy): JUCE paints an AlertWindow's message as text that cannot be selected, so the
// message is a read-only, multi-line TextEditor instead, placed first, before any combo box or
// button added afterwards. Return and Escape still reach the window's buttons. No icon: JUCE draws
// it over the top left corner, where the message now starts, so the title says what kind of
// question it is.
class SelectableAlertWindow final : public juce::AlertWindow {
public:
    SelectableAlertWindow(const juce::String& title, const juce::String& message,
                          juce::Component* associatedComponent);

private:
    juce::TextEditor messageText;
};

// Shows a SelectableAlertWindow with `buttons`, asynchronously, as juce::AlertWindow::showAsync
// does with MessageBoxOptions: the first button answers 1 and takes Return, the last answers 0 and
// takes Escape (a single button answers 0 and takes both), a middle one answers 2. `onResult` is
// called with that answer, also when the window is closed by a key.
void showSelectableAlert(const juce::String& title, const juce::String& message, const juce::StringArray& buttons, juce::Component* associatedComponent,
                         std::function<void(int)> onResult);
