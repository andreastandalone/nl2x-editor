#include "SelectableAlert.h"

#include "NordLookAndFeel.h"

#include <cmath>

namespace {

// The message's width; the window is a quarter wider (AlertWindow lays custom components out so).
constexpr int kMessageWidth = 460;
// Taller messages scroll.
constexpr int kMostMessageHeight = 420;

int heightOfText(const juce::String& text, const juce::Font& font, int width)
{
    juce::AttributedString attributed;
    attributed.append(text, font);
    attributed.setWordWrap(juce::AttributedString::byWord);
    juce::TextLayout layout;
    layout.createLayout(attributed, static_cast<float>(width));
    return static_cast<int>(std::ceil(layout.getHeight()));
}

} // namespace

void setUpSelectableMessage(juce::TextEditor& editor, const juce::String& message, const juce::Font& font,
                            juce::Colour colour, int width, int mostHeight)
{
    editor.setMultiLine(true, true);
    editor.setReadOnly(true);
    editor.setCaretVisible(false);
    editor.setScrollbarsShown(true);
    editor.setPopupMenuEnabled(true);
    editor.setEscapeAndReturnKeysConsumed(false);
    editor.setColour(juce::TextEditor::textColourId, colour);
    editor.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    editor.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    editor.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    editor.setColour(juce::TextEditor::shadowColourId, juce::Colours::transparentBlack);
    editor.setIndents(0, 0);
    editor.setBorder({});
    editor.setFont(font);
    editor.setText(message, false);
    // A little slack below the last line, so that no scroll bar appears for a message that fits.
    const auto height = heightOfText(message, font, width - 2) + static_cast<int>(font.getHeight());
    editor.setSize(width, juce::jmin(height, mostHeight));
}

SelectableAlertWindow::SelectableAlertWindow(const juce::String& title, const juce::String& message,
                                             juce::Component* associatedComponent)
    : juce::AlertWindow(title, {}, juce::MessageBoxIconType::NoIcon, associatedComponent)
{
    // The editor's look, as the window's other components have it.
    if (associatedComponent != nullptr)
        setLookAndFeel(&associatedComponent->getLookAndFeel());

    setUpSelectableMessage(messageText, message, NordFonts::body(), findColour(juce::AlertWindow::textColourId),
                           kMessageWidth, kMostMessageHeight);
    addCustomComponent(&messageText);
}

void showSelectableAlert(const juce::String& title, const juce::String& message, const juce::StringArray& buttons, juce::Component* associatedComponent,
                         std::function<void(int)> onResult)
{
    auto* window = new SelectableAlertWindow(title, message, associatedComponent);
    for (int i = 0; i < buttons.size(); ++i) {
        const bool first = i == 0;
        const bool last = i == buttons.size() - 1;
        const int answer = last ? 0 : first ? 1 : 2;
        const auto key = first && ! last ? juce::KeyPress(juce::KeyPress::returnKey)
                                         : last ? juce::KeyPress(juce::KeyPress::escapeKey) : juce::KeyPress();
        if (first && last)
            window->addButton(buttons[i], answer, juce::KeyPress(juce::KeyPress::escapeKey),
                              juce::KeyPress(juce::KeyPress::returnKey));
        else
            window->addButton(buttons[i], answer, key);
    }
    window->enterModalState(true, juce::ModalCallbackFunction::create([done = std::move(onResult)](int result) {
                                if (done)
                                    done(result);
                            }),
                            true);
}
