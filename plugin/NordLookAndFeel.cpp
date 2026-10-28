#include "NordLookAndFeel.h"

namespace P = NordPalette;

NordLookAndFeel::NordLookAndFeel()
{
    setColourScheme({ P::background, P::panelGrey, P::control, P::outline, P::text, P::controlOn, P::text,
                      P::controlHover, P::text });

    setColour(juce::ResizableWindow::backgroundColourId, P::background);
    setColour(juce::DocumentWindow::backgroundColourId, P::background);
    setColour(juce::Label::textColourId, P::text);
    setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Label::outlineWhenEditingColourId, P::outline);
    setColour(juce::TextButton::buttonColourId, P::control);
    setColour(juce::TextButton::buttonOnColourId, P::controlOn);
    setColour(juce::TextButton::textColourOffId, P::text);
    setColour(juce::TextButton::textColourOnId, P::textOnRed);
    setColour(juce::ToggleButton::textColourId, P::text);
    setColour(juce::ToggleButton::tickColourId, P::ledOn);
    setColour(juce::ToggleButton::tickDisabledColourId, P::ledOff);
    setColour(juce::ComboBox::backgroundColourId, P::control);
    setColour(juce::ComboBox::textColourId, P::text);
    setColour(juce::ComboBox::outlineColourId, P::control);
    setColour(juce::ComboBox::arrowColourId, P::textDim);
    setColour(juce::ComboBox::focusedOutlineColourId, P::outline);
    setColour(juce::PopupMenu::backgroundColourId, P::control);
    setColour(juce::PopupMenu::textColourId, P::text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, P::casingRed);
    setColour(juce::PopupMenu::highlightedTextColourId, P::textOnRed);
    setColour(juce::TextEditor::backgroundColourId, P::panelGrey.darker(0.35f));
    setColour(juce::TextEditor::textColourId, P::text);
    setColour(juce::TextEditor::outlineColourId, P::control);
    setColour(juce::TextEditor::focusedOutlineColourId, P::outline);
    setColour(juce::TreeView::backgroundColourId, P::panelGrey.darker(0.35f));
    setColour(juce::TreeView::selectedItemBackgroundColourId, P::casingRed.withAlpha(0.6f));
    setColour(juce::TreeView::linesColourId, P::textDim);
    setColour(juce::ScrollBar::thumbColourId, P::outline);
    setColour(juce::Slider::textBoxTextColourId, P::text);
    setColour(juce::Slider::textBoxBackgroundColourId, P::control);
    setColour(juce::Slider::textBoxOutlineColourId, P::control);
    setColour(juce::TooltipWindow::backgroundColourId, P::control);
    setColour(juce::TooltipWindow::textColourId, P::text);
    setColour(juce::TooltipWindow::outlineColourId, P::outline);
    setColour(juce::AlertWindow::backgroundColourId, P::panelGrey);
    setColour(juce::AlertWindow::textColourId, P::text);
    setColour(juce::AlertWindow::outlineColourId, P::outline);
    setColour(juce::TabbedButtonBar::tabOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::TabbedButtonBar::frontOutlineColourId, juce::Colours::transparentBlack);
}

void NordLookAndFeel::drawSection(juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& title)
{
    const auto area = bounds.toFloat();
    g.setColour(P::panelGrey);
    g.fillRoundedRectangle(area, NordMetrics::cornerRadius);

    auto bar = area.withHeight(static_cast<float>(NordMetrics::sectionTitleHeight));
    juce::Path titleBar;
    titleBar.addRoundedRectangle(bar.getX(), bar.getY(), bar.getWidth(), bar.getHeight(), NordMetrics::cornerRadius,
                                 NordMetrics::cornerRadius, true, true, false, false);
    g.setColour(P::sectionTitle);
    g.fillPath(titleBar);
    g.setColour(P::sectionTitleText);
    g.setFont(NordFonts::sectionTitle());
    g.drawText(title.toLowerCase(), bar.reduced(8.0f, 0.0f), juce::Justification::centredLeft, true);
}

void NordLookAndFeel::drawLed(juce::Graphics& g, juce::Rectangle<float> bounds, bool on)
{
    const auto size = std::min(bounds.getWidth(), bounds.getHeight());
    const auto led = bounds.withSizeKeepingCentre(size, size);
    g.setColour(on ? P::ledOn : P::ledOff);
    g.fillEllipse(led);
}

void NordLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                           bool highlighted, bool down)
{
    auto colour = backgroundColour;
    if (! button.isEnabled())
        colour = colour.withMultipliedAlpha(0.5f);
    else if (down)
        colour = colour.brighter(0.2f);
    else if (highlighted)
        colour = colour.brighter(0.1f);
    g.setColour(colour);
    g.fillRoundedRectangle(button.getLocalBounds().toFloat(), NordMetrics::cornerRadius);
}

void NordLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool)
{
    // As the panel's LED switches: an LED on a flat pill, with the text (or On/Off) beside it.
    auto area = button.getLocalBounds().toFloat();
    const bool on = button.getToggleState();
    const auto text = button.getButtonText().isNotEmpty() ? button.getButtonText() : juce::String(on ? "On" : "Off");
    const auto pill = area.withWidth(std::min(area.getWidth(), 56.0f)).reduced(0.0f, 2.0f);
    if (button.getButtonText().isEmpty()) {
        g.setColour(P::control.withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.5f));
        g.fillRoundedRectangle(pill, pill.getHeight() * 0.5f);
    }
    const auto ledSize = 8.0f;
    area.removeFromLeft(4.0f);
    drawLed(g, area.removeFromLeft(ledSize + 6.0f).withSizeKeepingCentre(ledSize, ledSize), on);
    auto colour = button.findColour(juce::ToggleButton::textColourId);
    if (! button.isEnabled())
        colour = colour.withMultipliedAlpha(0.5f);
    else if (highlighted)
        colour = colour.brighter(0.2f);
    g.setColour(colour);
    g.setFont(NordFonts::body());
    g.drawFittedText(text, area.toNearestInt(), juce::Justification::centredLeft, 2);
}

void NordLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int,
                                   juce::ComboBox& box)
{
    juce::Rectangle<float> area(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
    g.setColour(box.findColour(juce::ComboBox::backgroundColourId).withMultipliedAlpha(box.isEnabled() ? 1.0f : 0.5f));
    g.fillRoundedRectangle(area, NordMetrics::cornerRadius);

    // A small flat triangle.
    const auto arrow = area.removeFromRight(20.0f).withSizeKeepingCentre(8.0f, 5.0f);
    juce::Path path;
    path.addTriangle(arrow.getX(), arrow.getY(), arrow.getRight(), arrow.getY(), arrow.getCentreX(), arrow.getBottom());
    g.setColour(box.findColour(juce::ComboBox::arrowColourId).withMultipliedAlpha(box.isEnabled() ? 1.0f : 0.4f));
    g.fillPath(path);
}

juce::Font NordLookAndFeel::getComboBoxFont(juce::ComboBox& box)
{
    return juce::Font(NordFonts::body()).withHeight(std::min(13.5f, static_cast<float>(box.getHeight()) * 0.6f));
}

juce::Font NordLookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight)
{
    return juce::Font(NordFonts::body()).withHeight(std::min(13.5f, static_cast<float>(buttonHeight) * 0.6f));
}

void NordLookAndFeel::drawTabButton(juce::TabBarButton& button, juce::Graphics& g, bool isMouseOver, bool)
{
    const bool front = button.isFrontTab();
    auto area = button.getLocalBounds().toFloat().reduced(1.0f, 0.0f);
    juce::Path tab;
    tab.addRoundedRectangle(area.getX(), area.getY(), area.getWidth(), area.getHeight(), NordMetrics::cornerRadius,
                            NordMetrics::cornerRadius, true, true, false, false);
    g.setColour(front ? P::casingRed : (isMouseOver ? P::controlHover : P::control));
    g.fillPath(tab);
    g.setColour(front ? P::textOnRed : P::textDim);
    g.setFont(front ? juce::Font(NordFonts::heading()).withHeight(14.0f) : juce::Font(NordFonts::body()));
    g.drawText(button.getButtonText(), area, juce::Justification::centred, true);
}

int NordLookAndFeel::getTabButtonBestWidth(juce::TabBarButton& button, int)
{
    const auto text = button.getButtonText();
    return juce::jmax(60, juce::GlyphArrangement::getStringWidthInt(juce::Font(NordFonts::heading()), text) + 28);
}

void NordLookAndFeel::drawTabbedButtonBarBackground(juce::TabbedButtonBar&, juce::Graphics&) {}

void NordLookAndFeel::drawTabAreaBehindFrontButton(juce::TabbedButtonBar&, juce::Graphics& g, int w, int h)
{
    // A red line under the tabs, joining the front tab to the view it shows.
    g.setColour(P::casingRed);
    g.fillRect(0, h - 2, w, 2);
}

void NordLookAndFeel::drawTextEditorOutline(juce::Graphics&, int, int, juce::TextEditor&) {}

void NordLookAndFeel::fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    g.setColour(editor.findColour(juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), NordMetrics::cornerRadius);
}

void NordLookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height)
{
    g.fillAll(findColour(juce::TooltipWindow::backgroundColourId));
    g.setColour(findColour(juce::TooltipWindow::textColourId));
    g.setFont(NordFonts::small());
    g.drawFittedText(text, juce::Rectangle<int>(width, height).reduced(6, 3), juce::Justification::centredLeft, 8);
}
