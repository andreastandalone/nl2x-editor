#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// The editor's design system (docs/design/visual-style.md): every colour, font and size the GUI
// uses is named here, so that the look changes in one file. Flat and minimal: no gradients,
// shadows, bevels or textures anywhere.
//
// Colours marked PLACEHOLDER are guesses at the Nord Lead 2X's own colours, to be sampled from a
// photo of the user's synth (#13); change only the values, never the names.
namespace NordPalette {

// The red of the synth's casing: the header bar and the frame around the panel. PLACEHOLDER.
inline const juce::Colour casingRed { 0xffd0202e };
// The grey of the front panel: the body of every section. PLACEHOLDER.
inline const juce::Colour panelGrey { 0xff5a5d63 };
// Behind the sections: the gaps between them, the Library and the other views.
inline const juce::Colour background { 0xff303236 };
// The blue-violet/lilac of the panel's section title bars. PLACEHOLDER.
inline const juce::Colour sectionTitle { 0xff8a7cf0 };
inline const juce::Colour sectionTitleText { 0xff1c1b26 };
// The Morph Amount arc and everything that shows Morph (the Morph button, "M +12"). The same
// blue-violet as the title bars until the photo says otherwise. PLACEHOLDER.
inline const juce::Colour morph { 0xff8a7cf0 };

inline const juce::Colour text { 0xfff0f0f2 };
inline const juce::Colour textDim { 0xffb4b6bc };
inline const juce::Colour textOnRed { 0xffffffff };

// Arc knobs: the track and the value arc.
inline const juce::Colour knobTrack { 0xff3a3c41 };
inline const juce::Colour valueArc { 0xffe8e8ec };
// The LEDs of stepped choices and switches, as the synth's red LEDs. PLACEHOLDER.
inline const juce::Colour ledOn { 0xffff3b3b };
inline const juce::Colour ledOff { 0xff35363b };

// Buttons, menus and text boxes (flat fills).
inline const juce::Colour control { 0xff44464c };
inline const juce::Colour controlHover { 0xff50535a };
inline const juce::Colour controlOn { 0xffd0202e };
inline const juce::Colour outline { 0xff6c6f77 };

// States.
inline const juce::Colour outOfRange { 0xffe0a020 };
inline const juce::Colour warning { 0xffffb040 };
inline const juce::Colour statusGood { 0xff3cb043 };
inline const juce::Colour statusBusy { 0xffe0a020 };
inline const juce::Colour statusBad { 0xffff4a3d };
inline const juce::Colour statusOff { 0xff8a8a8a };

} // namespace NordPalette

namespace NordFonts {

// Section title bars, as the panel prints them.
inline juce::FontOptions sectionTitle() { return juce::FontOptions(13.0f, juce::Font::bold); }
// The names of knobs, choices and switches inside a section.
inline juce::FontOptions controlName() { return juce::FontOptions(11.5f); }
inline juce::FontOptions choice() { return juce::FontOptions(11.0f); }
// The value printed under a knob.
inline juce::FontOptions value() { return juce::FontOptions(12.5f, juce::Font::bold); }
// Ordinary text: labels, status lines, the Library.
inline juce::FontOptions body() { return juce::FontOptions(13.5f); }
inline juce::FontOptions small() { return juce::FontOptions(12.0f); }
inline juce::FontOptions heading() { return juce::FontOptions(15.0f, juce::Font::bold); }
inline juce::FontOptions header() { return juce::FontOptions(17.0f, juce::Font::bold); }

} // namespace NordFonts

namespace NordMetrics {

inline constexpr int gap = 8;
inline constexpr float cornerRadius = 4.0f;
inline constexpr int sectionTitleHeight = 20;
// One arc knob with its name above and its value below.
inline constexpr int knobWidth = 64;
inline constexpr int knobHeight = 88;
// One row of a list of named choices.
inline constexpr int choiceRowHeight = 15;
inline constexpr int switchHeight = 20;
inline constexpr int buttonHeight = 26;
inline constexpr int headerHeight = 44;

} // namespace NordMetrics

// The LookAndFeel of the whole editor: JUCE's own widgets (buttons, menus, tabs, text boxes,
// scroll bars) drawn flat in the palette above.
class NordLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    NordLookAndFeel();

    // A section of the panel: the body in panel grey with a title bar in blue-violet.
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NordLookAndFeel)
};
