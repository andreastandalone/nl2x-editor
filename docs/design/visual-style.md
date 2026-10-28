# Visual style

Applies to both the first functional version and the later polished one; polishing refines this style, it does not replace it.

## Direction

- **Flat and minimal**, like `skin-examples/codeknobs.png`: arc knobs with the value printed underneath, section panels with a title bar, no skeuomorphic shading, bevels or textures.
- **Not** like `skin-examples/soundtower.png` (photo-real knobs, heavy framing, cluttered labels).
- **Colours from the Nord Lead 2X itself**: Nord red, panel grey and the blue-violet/lilac of the panel sections. Exact values to be sampled from a photo of the user's synth.
- **Layout mirrors the hardware front panel**, as `skin-examples/nodalred.jpg` does: LFO 1, LFO 2/Arp and Mod Env on the left; Oscillators in the centre; Amplifier and Filter on the right; Master Level, Slot A–D selection, Play Mode, Portamento, Unison, Oct Shift and Mod Wheel destination along the bottom. A knob is found in the plugin where it sits on the synth.

## Where the palette lives

All colours, fonts and sizes are named in `plugin/NordLookAndFeel.h`; the components only use those names. The colours of the Nord Lead 2X are placeholders until sampled from the photo (#13), marked PLACEHOLDER there:

- `NordPalette::casingRed`: the red casing (header bar, panel frame, front tab).
- `NordPalette::panelGrey`: the front panel, the body of every section.
- `NordPalette::sectionTitle`, `sectionTitleText`: the blue-violet/lilac title bars and their text.
- `NordPalette::morph`: the Morph Amount arcs and the Morph button.
- `NordPalette::ledOn`, `ledOff`: the LEDs of stepped choices and switches.
- The others (`background`, `text`, `textDim`, `knobTrack`, `valueArc`, `control`, the status colours) are the editor's own.

## References

- `skin-examples/codeknobs.png`: liked, the reference for rendering style.
- `skin-examples/soundtower.png`: disliked, the counter-example.
- `skin-examples/nodalred.jpg`: Nodal Red 2X emulator, the reference for panel layout.
- Photo of the user's own Nord Lead 2X: pending, the reference for exact placement and colours.

The screenshots are third-party work used only as reference; none of their artwork is copied into the plugin.
