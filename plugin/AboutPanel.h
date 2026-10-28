#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

// The About panel, shown over the main view by the About button: what the editor is, its version,
// its licence and repository, the JUCE credit and the trademark notice.
class AboutPanel final : public juce::Component {
public:
    AboutPanel();

    void paint(juce::Graphics& g) override;
    void resized() override;

    std::function<void()> onClose;

private:
    juce::TextButton closeButton { "Close" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AboutPanel)
};
