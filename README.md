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

The code layout and the rules every change follows are in [CODING_STANDARDS.md](CODING_STANDARDS.md).

## Documentation

- [Glossary](GLOSSARY.md): the terms used throughout the project.
- [Decisions](docs/adr/): why the project is built the way it is.
- [MIDI/SysEx research](docs/research/nl2x-midi-research.md): what we know about the synth's protocol, with sources.

The official Nord Lead 2X manual and factory banks are not included; download them from the [Nord legacy downloads page](https://www.nordkeyboards.com/legacy-products/nord-lead-2x/downloads/).

## License

[GNU AGPLv3](LICENSE).

Not affiliated with or endorsed by Clavia DMI AB. Nord and Nord Lead are trademarks of Clavia DMI AB.
