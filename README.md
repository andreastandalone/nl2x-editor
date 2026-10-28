# NL2X Editor

An editor for the Nord Lead 2X: a free, open-source VST3/AU plugin for macOS that lets you edit every parameter of all four Slots from your DAW, automate them, and manage Programs, Performances and Banks as `.syx` files.

**Status:** early development, not usable yet.

## Planned features

- All sound parameters of Slots A–D, including the Velocity/Morph amounts the synth's panel hides.
- DAW automation and controller mapping (tested with Ableton Live and Cubase).
- Two-way sync: turning a knob on the synth moves it in the plugin.
- Your DAW project recalls the synth's sounds when it is reopened.
- A Library of standard `.syx` files with instant audition, and safe writing to the synth's memory with automatic backups.

## Requirements

- macOS (Intel or Apple Silicon).
- A Nord Lead 2X or Nord Rack 2X connected through a MIDI interface, with both MIDI In and Out cabled.

## Documentation

- [Glossary](GLOSSARY.md): the terms used throughout the project.
- [Decisions](docs/adr/): why the project is built the way it is.
- [MIDI/SysEx research](docs/research/nl2x-midi-research.md): what we know about the synth's protocol, with sources.

The official Nord Lead 2X manual and factory banks are not included; download them from the [Nord legacy downloads page](https://www.nordkeyboards.com/legacy-products/nord-lead-2x/downloads/).

## License

[GNU AGPLv3](LICENSE).

Not affiliated with or endorsed by Clavia DMI AB. Nord and Nord Lead are trademarks of Clavia DMI AB.
