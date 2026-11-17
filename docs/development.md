# Development

How to build, test and look at NL2X Editor, and what the current version does not cover yet. The code layout and the rules every change follows are in [CODING_STANDARDS.md](../CODING_STANDARDS.md); agent notes from past sessions are in [docs/agents/wiki/](agents/wiki/index.md).

## Building

You need macOS 11 or later, Xcode (with its command-line tools) and CMake 3.25 or later. JUCE 9.0.4 and Catch2 3.9.1 are downloaded by CMake at pinned versions, so the first configure needs an internet connection.

Build the plugin (VST3, AU and the standalone app, as Universal binaries for Intel and Apple Silicon):

```sh
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
```

The results land in `build/plugin/NL2XEditor_artefacts/Release/`, in `VST3/NL2X Editor.vst3`, `AU/NL2X Editor.component` and `Standalone/NL2X Editor.app`. The build does not install anything: to try the plugin in your DAW, copy the bundles yourself into `~/Library/Audio/Plug-Ins/VST3/` and `~/Library/Audio/Plug-Ins/Components/`.

Run the tests (they use a simulated Nord Lead 2X, so no synth is needed):

```sh
ctest --test-dir build --output-on-failure
```

To work on the engine without building JUCE, which is much faster, turn the plugin off:

```sh
cmake -S . -B build/engine -DNL2X_BUILD_PLUGIN=OFF
cmake --build build/engine -j8
ctest --test-dir build/engine --output-on-failure
```

Other useful options:

- `-DCMAKE_OSX_ARCHITECTURES=x86_64` (or `arm64`) builds for your Mac only, in about half the time.
- `-DNL2X_DOWNLOAD_CACHE=<dir>` keeps the downloaded JUCE and Catch2 archives in `<dir>`, so a fresh build directory does not download them again.

Check the VST3 with [pluginval](https://github.com/Tracktion/pluginval); the script downloads pluginval into `build/pluginval` the first time:

```sh
tools/run-pluginval.sh "build/plugin/NL2XEditor_artefacts/Release/VST3/NL2X Editor.vst3"
```

## Rendering the editor

To look at the editor without launching the app or a DAW, render it into a PNG. `nl2x_render_editor` (built with the plugin and the tests, never installed) builds the real editor offscreen on a simulated Nord Lead 2X that has answered the editor's reads, and paints it into a file. It touches neither your settings, your MIDI ports nor your Library: it keeps everything in `--work` (default: a folder in the system's temporary directory).

```sh
cmake --build build -j8 --target nl2x_render_editor
build/tools/render-editor/nl2x_render_editor --out slot-a.png
build/tools/render-editor/nl2x_render_editor --out morph.png --view B --morph
```

Options:

- `--view A|B|C|D|performance|librarian`: the tab shown. The Librarian is shown with the simulated synth's Banks read and a few locations to send.
- `--bank <0-3>`: the Bank the Librarian shows. `--edit <1-99>`: the Library edits that location of it.
- `--morph`: Morph mode on.
- `--settings`, `--about`, `--check`: the Settings panel open, the About panel open, the Connection check run and shown.
- `--open-library`: every group (subfolder) of the Library open. `--favourites`: its "Favourites only" filter on.
- `--kit A|B|C|D`: that Slot plays a Percussion Kit.
- `--offline`: the synth answers nothing.
- `--edge-cases`: Slots A and B on one MIDI channel, values outside the documented ranges and Morph Amounts past the ends of the ranges in Slot A.
- `--width <px>`: the window resized as by its corner, the aspect ratio kept. `--scale <n>`: pixel density, 1 to 4.
- `--dialog reading|store|manual|writing|done|failed`: instead of the editor, the Librarian's task window of a Store of Slot A to Bank 3, Program 1, while the lit Slot A is on Bank 0, in one of its phases: `reading` the backup, the confirmation (`store` when the editor switches the lit Slot, `manual` when Slot A's MIDI channel is off and the user sets the Bank by hand), `writing` after Overwrite, `done` the outcome, or `failed`, the outcome when the synth ignores the switch.
- `--work <dir>`: where it keeps its files.

The README's screenshot, `docs/images/editor.png`, is rendered with `--out docs/images/editor.png --view A --scale 2`; render it again when the look changes.

## Releases

How a version is checked and published: [release-process.md](release-process.md) and the [hardware checklist](hardware-checklist.md). A release attaches one zip per format with a stable name (`NL2X-Editor-VST3.zip`, `NL2X-Editor-AU.zip`, `NL2X-Editor-App.zip`), which the README links by tag.

## Not covered yet

What the current version (0.1.0-beta.1) does not do, kept here rather than in the README:

- **Percussion Kits:** no editing, and no Kit Banks in the Librarian. A Slot playing a Kit shows a notice instead of its parameters. Kits in the Library can be auditioned, saved as `.syx` and sent from the Library. The synth's replies to Kit Bank reads change from read to read, so reading and writing Kit Banks is not understood yet ([#33](https://github.com/andreastandalone/nl2x-editor/issues/33)).
- **Performances:** the Performance view is read-only. The synth stores a Performance dump it receives but does not apply it, so editing Split, Layer or Slot Channels from the editor would not change what it plays ([#31](https://github.com/andreastandalone/nl2x-editor/issues/31)). Performance automation parameters are refused for the same reason. Performances can be selected, auditioned from the Library and saved as `.syx`; writing Performance Banks is not supported ([#34](https://github.com/andreastandalone/nl2x-editor/issues/34)). Project recall restores the Slots' sounds, not the Performance settings.
- **After a Performance audition** (clicking a Performance in the Library), a Store asks the user to switch the lit Slot to the target Bank by hand, because the synth then reports the sent Performance instead of what its panel shows.
- **Banks 4–9** are the synth's ROM: they can be selected and played, not written. The Librarian shows only the RAM Banks 0–3.
- **Writing** expects P and C on (Shift + PROG/CTRL), and the lit Slot on its own MIDI channel for the automatic Bank switch.
- **macOS only**, 11 or later. Not signed with a Developer ID and not notarized.
- **Hosts:** tested in Ableton Live and Cubase (VST3); the AU passes `auval`. Corner resizing and the size menu have not been checked by hand in every host.

## Still unverified on hardware

These rest on the manual or on our own inference and are listed in `engine/include/nl2x/HardwareAssumptions.h` and [open-questions.md](open-questions.md); protocol facts and their sources are in [research/nl2x-midi-research.md](research/nl2x-midi-research.md).

- **Morph Amount scale:** whether a Morph Amount moves its parameter in the units of the knob value, as the Morph ring draws it ([#41](https://github.com/andreastandalone/nl2x-editor/issues/41)).
- **Layered Slots** also receiving on the leftmost active Slot's channel (manual p.76–77).
- **The names of value 0** of the stepped buttons other than Filter Type and LFO2 Dest/Arp Mode (their order is verified, the names are inferred), and Filter Distortion as CC80 driving bit 4 of byte 52.
- **Knob CCs** mapping one to one onto the SysEx values, in both directions; reverse CC sweeps.
- **The "off" Slot Channel** stored as −1 in a Performance.
- **A kept Performance dump:** whether selecting a Program or Performance on the panel replaces a Performance the synth received.
- **Kit reads:** a read of a Slot holding a Kit is reliable only for the lit Slot ([#33](https://github.com/andreastandalone/nl2x-editor/issues/33)).
- **Bank 0 Programs 1–4 and 17** were found empty once during development, with no write from the editor that could explain it ([#32](https://github.com/andreastandalone/nl2x-editor/issues/32)). Compare your Banks with your backups after the first writes, and report anything unexpected.

## Documentation

- [Glossary](../GLOSSARY.md): the terms used throughout the project.
- [Decisions](adr/): why the project is built the way it is.
- [MIDI/SysEx research](research/nl2x-midi-research.md): what we know about the synth's protocol, with sources.
- [User guide](user-guide.md): how each feature works, for users.

The official Nord Lead 2X manual and factory banks are not included; download them from the [Nord legacy downloads page](https://www.nordkeyboards.com/legacy-products/nord-lead-2x/downloads/).
