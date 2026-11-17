#include "AboutPanel.h"

#include "NordLookAndFeel.h"

namespace {

constexpr int kTextWidth = 700;
constexpr int kParagraphGap = 8;

const char* const kRepository = "https://github.com/andreastandalone/nl2x-editor";

} // namespace

AboutPanel::AboutPanel()
{
    const auto add = [this](const juce::String& text, juce::FontOptions font, juce::Colour colour, int height) {
        paragraphs.push_back({ text, font, colour, height });
    };
    add(juce::String::fromUTF8("NL2X Editor \xE2\x80\x93 an editor for the Nord Lead 2X"), NordFonts::header(),
        NordPalette::text, 28);
    add(juce::String("Version ") + NL2X_VERSION_STRING, NordFonts::body(), NordPalette::textDim, 20);
    add("This is a beta: Percussion Kits and Performances cannot be edited yet. Kits can be auditioned, "
        "saved and sent from the Library; the Performance view shows the synth's Performance read-only. "
        "Back up your Banks before writing to the synth's memory.",
        NordFonts::small(), NordPalette::text, 40);
    add("Free software under the GNU Affero General Public License, version 3 (AGPLv3), with ABSOLUTELY "
        "NO WARRANTY. You may redistribute and modify it under the terms of that licence.",
        NordFonts::small(), NordPalette::text, 40);
    linkParagraph = paragraphs.size();
    add({}, NordFonts::body(), NordPalette::text, 22);
    add("By Andrea Simoncini and contributors. Made with JUCE (juce.com), used under the AGPLv3. VST is a "
        "trademark of Steinberg Media Technologies GmbH.",
        NordFonts::small(), NordPalette::text, 40);
    add("Nord, Nord Lead and Clavia are trademarks of Clavia DMI AB. NL2X Editor is not affiliated with or "
        "endorsed by Clavia DMI AB.",
        NordFonts::small(), NordPalette::textDim, 40);

    sourceLink.setButtonText(juce::String("Source code: ") + kRepository);
    sourceLink.setURL(juce::URL(kRepository));
    sourceLink.setFont(juce::Font(NordFonts::body()), false, juce::Justification::centredLeft);
    sourceLink.setColour(juce::HyperlinkButton::textColourId, NordPalette::sectionTitleText);
    addAndMakeVisible(sourceLink);

    closeButton.setComponentID("aboutClose");
    closeButton.onClick = [this] {
        if (onClose)
            onClose();
    };
    addAndMakeVisible(closeButton);
}

juce::Rectangle<int> AboutPanel::textArea() const
{
    auto area = getLocalBounds().reduced(NordMetrics::gap).withTrimmedTop(NordMetrics::sectionTitleHeight).reduced(24);
    return area.withWidth(std::min(area.getWidth(), kTextWidth));
}

void AboutPanel::paint(juce::Graphics& g)
{
    g.fillAll(NordPalette::background);
    NordLookAndFeel::drawSection(g, getLocalBounds().reduced(NordMetrics::gap), "About");

    auto area = textArea();
    for (const auto& paragraph : paragraphs) {
        const auto bounds = area.removeFromTop(paragraph.height);
        area.removeFromTop(kParagraphGap);
        if (paragraph.text.isEmpty())
            continue;
        g.setFont(paragraph.font);
        g.setColour(paragraph.colour);
        g.drawFittedText(paragraph.text, bounds, juce::Justification::topLeft, 3, 1.0f);
    }
}

void AboutPanel::resized()
{
    auto area = textArea();
    for (size_t i = 0; i < paragraphs.size(); ++i) {
        const auto bounds = area.removeFromTop(paragraphs[i].height);
        area.removeFromTop(kParagraphGap);
        if (i == linkParagraph) {
            sourceLink.setBounds(bounds);
            sourceLink.changeWidthToFitText();
        }
    }
    area.removeFromTop(kParagraphGap);
    closeButton.setBounds(area.removeFromTop(NordMetrics::buttonHeight).removeFromLeft(90));
}
