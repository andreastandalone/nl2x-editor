#include "PluginEditor.h"

#include <cmath>

namespace {

// The user settings key of the scale last chosen (1 = the design size).
constexpr auto kScaleKey = "editorScale";
// The window between three quarters and twice the design size: below, the smallest text stops
// being readable; above, it no longer fits any screen.
constexpr double kMinScale = 0.75;
constexpr double kMaxScale = 2.0;

int scaled(int size, double scale)
{
    return juce::roundToInt(size * scale);
}

} // namespace

NL2XPluginEditor::NL2XPluginEditor(NL2XEditorProcessor& owner)
    : AudioProcessorEditor(owner),
      editorProcessor(owner),
      content(owner)
{
    setWantsKeyboardFocus(true);
    addAndMakeVisible(content);
    // Once every child is in place: the change reaches them all (widgets built before it, such as
    // a slider's text box, are rebuilt in the new look).
    setLookAndFeel(&lookAndFeel);

    // The scale last chosen, read before the limits below resize the editor (and would store it).
    const auto scale = juce::jlimit(kMinScale, kMaxScale,
                                    editorProcessor.midiPorts().userSettings().getDoubleValue(kScaleKey, 1.0));
    const auto width = EditorContent::kWidth;
    const auto height = EditorContent::kHeight;
    setResizable(true, true);
    setResizeLimits(scaled(width, kMinScale), scaled(height, kMinScale), scaled(width, kMaxScale),
                    scaled(height, kMaxScale));
    getConstrainer()->setFixedAspectRatio(static_cast<double>(width) / height);
    setSize(scaled(width, scale), scaled(height, scale));
    storesScale = true;
}

NL2XPluginEditor::~NL2XPluginEditor()
{
    editorProcessor.midiPorts().userSettings().saveIfNeeded();
    setLookAndFeel(nullptr);
}

void NL2XPluginEditor::resized()
{
    const auto scale = static_cast<double>(getWidth()) / EditorContent::kWidth;
    content.setBounds(0, 0, EditorContent::kWidth, EditorContent::kHeight);
    content.setTransform(juce::AffineTransform::scale(static_cast<float>(scale)));
    auto& settings = editorProcessor.midiPorts().userSettings();
    if (storesScale && std::abs(settings.getDoubleValue(kScaleKey, 1.0) - scale) > 0.001)
        settings.setValue(kScaleKey, scale);
}

bool NL2XPluginEditor::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0)) {
        editorProcessor.undo();
        return true;
    }
    if (key == juce::KeyPress('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)) {
        editorProcessor.redo();
        return true;
    }
    return false;
}
