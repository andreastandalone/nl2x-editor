#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// The editor's design system (docs/design/visual-style.md): every colour, font and size the GUI
// uses is named here, so that the look changes in one file. Flat: no gradients, bevels or
// textures. A dark graphite panel, sections as slightly lighter plates with a quiet title strip,
// red used only as the accent (the front tab, LEDs, active buttons), the Morph ring in its own two
// colours. The look evokes a synth front panel without copying any maker's trade dress or logo.
namespace NordPalette {

// The accent: the red line under the header, the front tab, buttons that are on, menu highlights.
// (Named after the red casing of the Nord Lead 2X that the first look copied.)
inline const juce::Colour casingRed { 0xffe5343f };
// Behind everything: the window, the gaps between sections.
inline const juce::Colour background { 0xff141518 };
// The header bar and the plate the Program panel's sections sit on.
inline const juce::Colour headerBar { 0xff1c1e22 };
inline const juce::Colour plate { 0xff1c1e22 };
// The body of every section.
inline const juce::Colour panelGrey { 0xff272a30 };
// A section's title strip and its text (uppercase, letter-spaced).
inline const juce::Colour sectionTitle { 0xff30333a };
inline const juce::Colour sectionTitleText { 0xffc6beff };
// The Morph Amount ring and everything that shows Morph (the Morph button, "M +12"): a positive
// amount in `morph`, a negative one in `morphNegative`. Both stand apart from the title text, the
// value arc, the LEDs and `outOfRange`, so the ring never blends into the panel.
inline const juce::Colour morph { 0xff2ec4b6 };
inline const juce::Colour morphNegative { 0xffff7a45 };

// Text: bright, and dimmed for names and secondary lines. Both light enough to read on the
// graphite when the window is scaled down to 75 %.
inline const juce::Colour text { 0xfff6f7f9 };
inline const juce::Colour textDim { 0xffbcc1ca };
inline const juce::Colour textOnRed { 0xffffffff };
// Text on a light accent fill (the Morph button when on).
inline const juce::Colour textOnAccent { 0xff111316 };

// Arc knobs: the track, the value arc, the cap in the middle and its pointer.
inline const juce::Colour knobTrack { 0xff3b3f47 };
inline const juce::Colour valueArc { 0xffeef0f3 };
inline const juce::Colour knobCap { 0xff1b1d21 };
// The LEDs of stepped choices and switches.
inline const juce::Colour ledOn { 0xffff3d3d };
inline const juce::Colour ledOff { 0xff3a3d44 };

// Buttons, menus and text boxes (flat fills), and hairlines.
inline const juce::Colour control { 0xff33363d };
inline const juce::Colour controlHover { 0xff3e424a };
inline const juce::Colour controlOn { 0xffe5343f };
inline const juce::Colour outline { 0xff4a4e57 };
inline const juce::Colour hairline { 0xff2c2f35 };

// States.
inline const juce::Colour outOfRange { 0xffe8b02a };
inline const juce::Colour warning { 0xffffb84d };
inline const juce::Colour statusGood { 0xff3fbf5a };
inline const juce::Colour statusBusy { 0xffe8b02a };
inline const juce::Colour statusBad { 0xffff5247 };
inline const juce::Colour statusOff { 0xff8f949c };

// The tags of a Library row (the category): a small rounded chip.
inline const juce::Colour tagFill { 0xff3d4350 };
inline const juce::Colour tagText { 0xffe3e6ec };

} // namespace NordPalette

// Avenir Next ships with every macOS the plugin supports (11 and later), in the weights used here;
// the condensed cut sets the titles. Nothing is bundled.
namespace NordFonts {

inline constexpr auto kSans = "Avenir Next";
inline constexpr auto kCondensed = "Avenir Next Condensed";

inline juce::FontOptions font(const char* name, const char* style, float height, float kerning = 0.0f)
{
    return juce::FontOptions().withName(name).withStyle(style).withHeight(height).withKerningFactor(kerning);
}

// Section titles, uppercase and letter-spaced.
inline juce::FontOptions sectionTitle() { return font(kCondensed, "Demi Bold", 15.5f, 0.08f); }
// The names of knobs, choices and switches inside a section.
inline juce::FontOptions controlName() { return font(kSans, "Medium", 14.5f); }
inline juce::FontOptions choice() { return font(kSans, "Medium", 14.5f); }
// The value printed under a knob.
inline juce::FontOptions value() { return font(kSans, "Demi Bold", 15.0f); }
// Ordinary text: labels, status lines, the Library.
inline juce::FontOptions body() { return font(kSans, "Regular", 16.0f); }
inline juce::FontOptions small() { return font(kSans, "Regular", 15.5f); }
inline juce::FontOptions heading() { return font(kSans, "Demi Bold", 17.5f); }
// The tags of a Library row.
inline juce::FontOptions tag() { return font(kSans, "Demi Bold", 13.5f); }
// The tabs above the view.
inline juce::FontOptions tab() { return font(kSans, "Regular", 16.5f); }
inline juce::FontOptions frontTab() { return font(kSans, "Demi Bold", 16.5f); }
// The name in the header bar.
inline juce::FontOptions header() { return font(kCondensed, "Bold", 21.5f, 0.06f); }

} // namespace NordFonts

namespace NordMetrics {

inline constexpr int gap = 8;
inline constexpr float cornerRadius = 5.0f;
inline constexpr int sectionTitleHeight = 20;
// One arc knob with its name above and its value below.
inline constexpr int knobWidth = 70;
inline constexpr int knobHeight = 90;
// One row of a list of named choices.
inline constexpr int choiceRowHeight = 17;
inline constexpr int switchHeight = 20;
inline constexpr int buttonHeight = 26;
inline constexpr int headerHeight = 44;

} // namespace NordMetrics

// The LookAndFeel of the whole editor: JUCE's own widgets (buttons, menus, tabs, text boxes,
// scroll bars) drawn flat in the palette above.
class NordLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    NordLookAndFeel();

    // A section of the panel: the body in panel grey with its title strip.
    static void drawSection(juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& title);
    // An LED: lit or not.
    static void drawLed(juce::Graphics& g, juce::Rectangle<float> bounds, bool on);

    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                              bool highlighted, bool down) override;
    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool down) override;
    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown, int buttonX, int buttonY,
                      int buttonW, int buttonH, juce::ComboBox& box) override;
    juce::Font getComboBoxFont(juce::ComboBox& box) override;
    juce::Font getTextButtonFont(juce::TextButton& button, int buttonHeight) override;
    void drawTabButton(juce::TabBarButton& button, juce::Graphics& g, bool isMouseOver, bool isMouseDown) override;
    int getTabButtonBestWidth(juce::TabBarButton& button, int tabDepth) override;
    void drawTabbedButtonBarBackground(juce::TabbedButtonBar& bar, juce::Graphics& g) override;
    void drawTabAreaBehindFrontButton(juce::TabbedButtonBar& bar, juce::Graphics& g, int w, int h) override;
    void drawTextEditorOutline(juce::Graphics& g, int width, int height, juce::TextEditor& editor) override;
    void fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor& editor) override;
    void drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height) override;
    void drawCornerResizer(juce::Graphics& g, int w, int h, bool isMouseOver, bool isMouseDragging) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NordLookAndFeel)
};
