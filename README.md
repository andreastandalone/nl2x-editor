# NL2X Editor

An editor for the Nord Lead 2X: a free, open-source VST3/AU plugin for macOS that lets you edit every parameter of all four Slots from your DAW, automate them, and manage Programs, Performances and Banks as `.syx` files.

**Status:** early development. Version 0.1, the first release, is in preparation: it is being checked on a real Nord Lead 2X and has not been published yet.

## Planned features

- All sound parameters of Slots A–D, including the Velocity/Morph amounts the synth's panel hides.
- DAW automation and controller mapping (tested with Ableton Live and Cubase).
- Two-way sync: turning a knob on the synth moves it in the plugin.
- Your DAW project recalls the synth's sounds when it is reopened.
- A Library of standard `.syx` files with instant audition, and safe writing to the synth's memory with automatic backups.

## Requirements

- macOS (Intel or Apple Silicon).
- A Nord Lead 2X or Nord Rack 2X connected through a MIDI interface, with both MIDI In and Out cabled.

## Installation

Once version 0.1 is published, download `NL2X-Editor-<version>-macOS-universal.zip` from the [GitHub Releases page](https://github.com/andreastandalone/nl2x-editor/releases) and unzip it. It contains:

- `NL2X Editor.vst3`: copy it to `~/Library/Audio/Plug-Ins/VST3/`.
- `NL2X Editor.component` (the Audio Unit): copy it to `~/Library/Audio/Plug-Ins/Components/`.
- `NL2X Editor.app` (the standalone app): copy it to `/Applications`.

(`~` is your home folder. In the Finder, choose Go > Go to Folder... and type the path.) Restart your DAW so that it rescans its plugins.

**Opening it the first time.** The build is not signed with an Apple Developer certificate and not notarized, so macOS blocks it with a message saying it cannot check it for malicious software. This is expected for a free, open-source build; the source code is public and you can build it yourself (see Building). To open it anyway:

1. Try to open the app (or load the plugin in your DAW) once, so that macOS shows the message and records the block.
2. Open System Settings > Privacy & Security, scroll down to the Security section and click **Open Anyway** next to "NL2X Editor". Confirm with your password. For a plugin, load it again in the DAW and repeat if macOS asks again.

Alternatively, in Terminal, remove the quarantine mark macOS put on the files you downloaded. This only affects the files you name, so check the path before pressing Return:

```sh
xattr -dr com.apple.quarantine "/Applications/NL2X Editor.app"
xattr -dr com.apple.quarantine "$HOME/Library/Audio/Plug-Ins/VST3/NL2X Editor.vst3"
xattr -dr com.apple.quarantine "$HOME/Library/Audio/Plug-Ins/Components/NL2X Editor.component"
```

Only do this for a file you downloaded from this project's Releases page.

## MIDI setup

The plugin talks to the synth directly, in both directions, so it needs both MIDI cables:

- the interface's MIDI Out to the synth's MIDI In, and
- the synth's MIDI Out to the interface's MIDI In.

Choose the MIDI input and output ports inside the plugin (its settings), not in the DAW. The plugin remembers them. Only one editor at a time should control the synth: if a second instance or application is open, the plugin warns you.

The plugin produces no audio: its single stereo output is silent. Notes still come from a normal MIDI track in your DAW sent to the synth, as you would do without the plugin. Play the synth's own sound through your audio interface as usual.

When the synth is connected, the plugin runs a Connection check and shows the result. If something is wrong (a missing cable, a setting on the synth), it says what to change.

## Synth settings

On the synth, press **Shift + PROG/CTRL** and set:

- **P** (Program Change) on,
- **C** (Control Change) on: the plugin edits parameters with control changes, so nothing works with C off,
- **A** (Arp MIDI Out) off.

The plugin also needs the synth's **Global Channel** (shown as `G.x` when you press **Shift + MIDI CH** twice). By default the plugin finds it automatically; if it cannot, choose it by hand in the plugin's settings. **Local** is always On after power-up, which is the right setting unless you route the synth's MIDI through your DAW with Local Off.

## Using it in Ableton Live and Cubase

Insert NL2X Editor on a MIDI track as an instrument (in Live: drag it from Plug-Ins > VST3 or Audio Units onto a MIDI track; in Cubase: Add Instrument Track or Rack Instrument, then choose it). Open its window and choose the MIDI ports. Then set that track's MIDI output to the synth's port, or use a second MIDI track for the notes, so the notes reach the synth. The parameters appear in the DAW's automation lists with names such as "A: Filter Cutoff", and your project remembers the synth's sounds.

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

To look at the editor without launching the app or a DAW, render it into a PNG. `nl2x_render_editor` (built with the plugin and the tests, never installed) builds the real editor offscreen on a simulated Nord Lead 2X that has answered the editor's reads, and paints it into a file. It touches neither your settings, your MIDI ports nor your Library: it keeps everything in `--work` (default: a folder in the system's temporary directory).

```sh
cmake --build build -j8 --target nl2x_render_editor
build/tools/render-editor/nl2x_render_editor --out slot-a.png
build/tools/render-editor/nl2x_render_editor --out morph.png --view B --morph
```

Options: `--view A|B|C|D|performance|memory` (the tab shown), `--morph` (Morph mode on), `--settings` (the Settings panel open), `--about` (the About panel open), `--check` (the Connection check run and shown), `--kit A|B|C|D` (that Slot plays a Percussion Kit), `--offline` (the synth answers nothing), `--edge-cases` (Slots A and B on one MIDI channel, values outside the documented ranges in Slot A), `--scale <n>` (pixel density, 1 to 4), `--work <dir>`.

The code layout and the rules every change follows are in [CODING_STANDARDS.md](CODING_STANDARDS.md).

## Documentation

- [Glossary](GLOSSARY.md): the terms used throughout the project.
- [Decisions](docs/adr/): why the project is built the way it is.
- [Hardware checklist](docs/hardware-checklist.md) and [release process](docs/release-process.md): how a release is checked and published.
- [MIDI/SysEx research](docs/research/nl2x-midi-research.md): what we know about the synth's protocol, with sources.

The official Nord Lead 2X manual and factory banks are not included; download them from the [Nord legacy downloads page](https://www.nordkeyboards.com/legacy-products/nord-lead-2x/downloads/).

## License

[GNU AGPLv3](LICENSE).

Not affiliated with or endorsed by Clavia DMI AB. Nord and Nord Lead are trademarks of Clavia DMI AB.
