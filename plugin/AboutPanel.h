#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

// The About panel, shown over the main view by the About button: what the editor is, its version
// (with its pre-release label, from the build), what this version cannot do yet, its licence and
// source code (a link), the credits and the trademark notice.
class AboutPanel final : public juce::Component {
public:
    AboutPanel();

    void paint(juce::Graphics& g) override;
    void resized() override;

    std::function<void()> onClose;

private:
    // One paragraph of the panel. The source link is a paragraph with no text: the link button
    // sits in its place.
    struct Paragraph {
        juce::String text;
        juce::FontOptions font;
        juce::Colour colour;
        int height;
    };

    juce::Rectangle<int> textArea() const;

    std::vector<Paragraph> paragraphs;
    size_t linkParagraph = 0;
    juce::HyperlinkButton sourceLink;
    juce::TextButton closeButton { "Close" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AboutPanel)
};
