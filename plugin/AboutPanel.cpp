#include "AboutPanel.h"

#include "NordLookAndFeel.h"

namespace {

constexpr int kTextWidth = 640;

const char* const kRepository = "https://github.com/andreastandalone/nl2x-editor";

} // namespace

AboutPanel::AboutPanel()
{
    closeButton.setComponentID("aboutClose");
    closeButton.onClick = [this] {
        if (onClose)
            onClose();
    };
    addAndMakeVisible(closeButton);
}

void AboutPanel::paint(juce::Graphics& g)
{
    g.fillAll(NordPalette::background);
    NordLookAndFeel::drawSection(g, getLocalBounds().reduced(NordMetrics::gap), "About");

    auto area = getLocalBounds().reduced(NordMetrics::gap).withTrimmedTop(NordMetrics::sectionTitleHeight).reduced(24);
    area = area.withWidth(std::min(area.getWidth(), kTextWidth));

    const auto line = [&](const juce::String& text, juce::FontOptions font, juce::Colour colour, int height) {
        g.setFont(font);
        g.setColour(colour);
        g.drawFittedText(text, area.removeFromTop(height), juce::Justification::topLeft, 3);
    };
    line(juce::String::fromUTF8("NL2X Editor \xE2\x80\x93 an editor for the Nord Lead 2X"), NordFonts::header(),
         NordPalette::text, 28);
    line("Version " JucePlugin_VersionString, NordFonts::body(), NordPalette::textDim, 30);
    line("Free software under the GNU Affero General Public License v3 (AGPLv3).", NordFonts::body(),
         NordPalette::text, 20);
    line(juce::String("Source code: ") + kRepository, NordFonts::body(), NordPalette::text, 30);
    line("Made with JUCE (juce.com), used under the AGPLv3.", NordFonts::body(), NordPalette::text, 30);
    line("Not affiliated with or endorsed by Clavia DMI AB. Nord and Nord Lead are trademarks of Clavia DMI AB.",
         NordFonts::small(), NordPalette::textDim, 36);
}

void AboutPanel::resized()
{
    auto area = getLocalBounds().reduced(NordMetrics::gap).withTrimmedTop(NordMetrics::sectionTitleHeight).reduced(24);
    area = area.withWidth(std::min(area.getWidth(), kTextWidth));
    area.removeFromTop(190);
    closeButton.setBounds(area.removeFromTop(NordMetrics::buttonHeight).removeFromLeft(90));
}
