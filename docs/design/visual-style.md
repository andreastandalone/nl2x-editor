# Visual style

Applies to both the first functional version and the later polished one; polishing refines this style, it does not replace it.

## Direction

- **Flat and minimal**, like `skin-examples/codeknobs.png`: arc knobs with the value printed underneath, section panels with a title bar, no skeuomorphic shading, bevels or textures.
- **Not** like `skin-examples/soundtower.png` (photo-real knobs, heavy framing, cluttered labels).
- **A dark graphite panel with one red accent** (2026-11-02, #42, replacing the first look's red casing, grey panel and violet title bars): sections are slightly lighter plates on a darker plate, with a quiet title strip in uppercase, letter-spaced lilac; red marks only what is active (the line under the header, the front tab, buttons that are on, LEDs). The look evokes a synth front panel without copying Clavia's trade dress or logo: no red casing, no Nord wordmark or typeface.
- **Typography**: Avenir Next (shipped with macOS) for everything, its condensed cut for titles and the header; knob names dim, values bright and bold.
- **Knobs**: a 270° track, the value arc in near-white, a dark cap with a pointer in the middle, the Morph ring between the two.
- **The window scales**: the editor is laid out once at its design size (1378 × 758) and scaled as a whole when the window is resized by its corner (75 %–200 %, aspect ratio kept); the scale is remembered in the user settings. No component lays itself out for other sizes.
- **Layout mirrors the hardware front panel**, as `skin-examples/nodalred.jpg` does: LFO 1, LFO 2/Arp and Mod Env on the left; Oscillators in the centre; Amplifier and Filter on the right; Play Mode, Portamento, Unison, Oct Shift and Mod Wheel destination along the bottom. A knob is found in the plugin where it sits on the synth. Two panel controls are left out (2026-11-02, #40): Master Level, which has no MIDI and is not part of the Program, and the Slot A–D buttons, since the Slot is chosen with the editor's tabs.

## The Morph ring

A morphable knob draws its Morph Amount as a thinner ring inside the value arc, from the value to value + amount (an unverified assumption, `morphTarget` in `HardwareAssumptions.h`). Positive amounts are teal, negative ones orange; a dot marks where the ring lands. When value + amount goes past the end of the range the ring stops there and a tick across the track marks the clipping, so a Morph Amount is never invisible.

## Where the palette lives

All colours, fonts and sizes are named in `plugin/NordLookAndFeel.h`; the components only use those names.

- `NordPalette::casingRed`: the red accent (named after the synth's casing, which the first look copied).
- `NordPalette::background`, `headerBar`, `plate`, `panelGrey`: from darkest to lightest, the window, the header and the Program panel's plate, the body of every section.
- `NordPalette::sectionTitle`, `sectionTitleText`: the title strip of a section and its text.
- `NordPalette::morph`, `morphNegative`: the Morph Amount ring (positive and negative amounts) and the Morph button; deliberately not the title colour.
- `NordPalette::knobTrack`, `valueArc`, `knobCap`: the knob.
- `NordPalette::ledOn`, `ledOff`: the LEDs of stepped choices and switches.
- The others (`text`, `textDim`, `textOnAccent`, `control`, `outline`, `hairline`, the status colours) are the editor's own.
- `NordFonts`: Avenir Next and Avenir Next Condensed, by role (section title, control name, choice, value, body, heading, header).

## References

- `skin-examples/codeknobs.png`: liked, the reference for rendering style.
- `skin-examples/soundtower.png`: disliked, the counter-example.
- `skin-examples/nodalred.jpg`: Nodal Red 2X emulator, the reference for panel layout.
- Photo of the user's own Nord Lead 2X: pending, the reference for exact placement.

The screenshots are third-party work used only as reference; none of their artwork is copied into the plugin.
