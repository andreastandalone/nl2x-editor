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

SelectableAlertWindow::SelectableAlertWindow(const juce::String& title, const juce::String& message,
                                             juce::Component* associatedComponent)
    : juce::AlertWindow(title, {}, juce::MessageBoxIconType::NoIcon, associatedComponent)
{
    // The editor's look, as the window's other components have it.
    if (associatedComponent != nullptr)
        setLookAndFeel(&associatedComponent->getLookAndFeel());

    const juce::Font font(NordFonts::body());
    messageText.setMultiLine(true, true);
    messageText.setReadOnly(true);
    messageText.setCaretVisible(false);
    messageText.setScrollbarsShown(true);
    messageText.setPopupMenuEnabled(true);
    // Return and Escape go on to the window's buttons.
    messageText.setEscapeAndReturnKeysConsumed(false);
    messageText.setColour(juce::TextEditor::textColourId, findColour(juce::AlertWindow::textColourId));
    messageText.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    messageText.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    messageText.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    messageText.setColour(juce::TextEditor::shadowColourId, juce::Colours::transparentBlack);
    messageText.setIndents(0, 0);
    messageText.setBorder({});
    messageText.setFont(font);
    messageText.setText(message, false);
    // A little slack below the last line, so that no scroll bar appears for a message that fits.
    const auto height = heightOfText(message, font, kMessageWidth - 2) + static_cast<int>(font.getHeight());
    messageText.setSize(kMessageWidth, juce::jmin(height, kMostMessageHeight));
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
