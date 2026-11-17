# NL2X Editor

An editor for the Nord Lead 2X: a free, open-source VST3/AU plugin and standalone app for macOS that lets you edit every sound parameter of the four Slots from your DAW, automate them, keep your sounds in a Library of `.syx` files and write them into the synth's Banks.

**Status: public beta, version 0.1.0-beta.1.** It has been used on a real Nord Lead 2X, but not by many people yet. Percussion Kits and Performances cannot be edited yet (see [Not in this beta](#not-in-this-beta)). Back up your Banks before you write to the synth (see [Safety](#safety)). What changed and what is still open is in the [release notes](docs/release-notes/0.1.0-beta.1.md).

## What the beta does

- **Edits the four Slots.** Every sound parameter of the Programs on Slots A–D, from the plugin's panel, from the synth's own panel or from the DAW, including the Velocity/Morph Amounts the synth's panel hides (Option-drag a knob).
- **Two-way.** Turn a knob on the synth and the plugin follows; move a control in the plugin and the synth follows.
- **DAW automation.** Every parameter is a DAW parameter, named like "A: Filter Cutoff" or "B: LFO1 Rate Morph", for automation and controller mapping. Tested with Ableton Live and Cubase.
- **Project recall.** Your DAW project remembers the four Slots' sounds and sends them to the synth when the project is opened (you can turn this off). It never stores anything in the synth's Banks.
- **Bank and Program selectors** for each Slot, and a Performance selector.
- **Connection check.** It finds the synth's Global Channel by itself and tells you what to change when a cable or a setting is wrong.
- **Library.** Your `.syx` files (single Programs, Percussion Kits, Performances and whole Banks), each subfolder a collapsible group. Click a Program to hear it at once on the selected Slot (Audition) without storing it. Your own names, tags and favourites for each sound, with filters; suggested tags and Auto-tag.
- **Librarian.** Reads the synth's four RAM Program Banks (0–3), shows them beside the Library, and lets you drag Programs into Bank locations, store a Slot or a Library Program into a location, and send a changed or whole Bank. Every write is backed up first and read back afterwards.
- **Undo and redo** of your edits (also Cmd+Z and Cmd+Shift+Z in the plugin window).
- **Saves** a Slot, a Performance, a Library entry or a Bank as standard `.syx` files that other tools understand.

## Not in this beta

- **Percussion Kits cannot be edited or managed in the Librarian.** A Slot playing a Kit shows a notice instead of its parameters. Kits that are in your Library can still be auditioned, saved as `.syx` and sent from the Library. Reading and writing the synth's Kit Banks is not understood yet ([#33](https://github.com/andreastandalone/nl2x-editor/issues/33)).
- **Performances cannot be edited.** The Performance view shows the synth's Performance (Slot Channels, Split, Layer) read-only: the synth stores a Performance sent to it but does not play it, so edits would have no effect ([#31](https://github.com/andreastandalone/nl2x-editor/issues/31)). Performances can be selected, auditioned from the Library and saved as `.syx`; writing them into the synth's Performance Banks is not supported ([#34](https://github.com/andreastandalone/nl2x-editor/issues/34)).
- **Banks 4–9** are the synth's ROM: they can be selected and played, not written. The Librarian shows only the RAM Banks 0–3.
- **macOS only**, and the build is not signed by an Apple Developer ID or notarized (see [Opening it the first time](#opening-it-the-first-time)).
- Some behaviour still rests on assumptions not yet checked on hardware; they are listed in the [release notes](docs/release-notes/0.1.0-beta.1.md#still-unverified-on-hardware).

## Requirements

- macOS 11 or later, on an Intel or Apple Silicon Mac.
- A Nord Lead 2X or Nord Rack 2X connected through a MIDI interface, with **both** MIDI In and MIDI Out cabled.
- For the plugin: a DAW that hosts VST3 or Audio Unit instruments (tested: Ableton Live and Cubase).

## Installation

Download `NL2X-Editor-0.1.0-beta.1-macOS-universal.zip` from the [GitHub Releases page](https://github.com/andreastandalone/nl2x-editor/releases) and double-click it to unzip it. The folder contains:

| File | What it is | Copy it to |
|---|---|---|
| `NL2X Editor.vst3` | the VST3 plugin | `/Library/Audio/Plug-Ins/VST3/` |
| `NL2X Editor.component` | the Audio Unit plugin | `/Library/Audio/Plug-Ins/Components/` |
| `NL2X Editor.app` | the standalone app | `/Applications/` |

In the Finder, choose Go > Go to Folder... and type the folder's path. macOS asks for your administrator password when you copy into `/Library` or `/Applications`. (To install for your user account only, use the same folders under your home folder instead: `~/Library/Audio/Plug-Ins/VST3/` and `~/Library/Audio/Plug-Ins/Components/`.) Install only the formats you need: Live and Cubase use the VST3, Logic uses the AU.

Then restart your DAW so that it rescans its plugins. In Live, if the plugin does not appear, turn on Settings > Plug-Ins > Use VST3 Plug-In System Folders (and Use Audio Units).

### Opening it the first time

The build is free and open source, so it is not signed with a paid Apple Developer ID and not notarized by Apple. macOS therefore blocks it the first time with a message such as "Apple could not verify "NL2X Editor" is free of malware". This is expected; the source code is public and you can build it yourself (see [Building](#building)). Allow it once in one of these two ways.

**System Settings (macOS 15 Sequoia and later, also works on earlier versions):**

1. Try to open the app, or load the plugin in your DAW, once. macOS shows the message; click **Done** (not Move to Trash).
2. Open System Settings > **Privacy & Security** and scroll down to the Security section. Next to "NL2X Editor was blocked to protect your Mac", click **Open Anyway** and confirm with your password or Touch ID. The button appears only for about an hour after the blocked attempt.
3. Open the app or reload the plugin again, and click **Open Anyway** (or Open) in the last dialog.

Do this for each format you use: the app, the VST3 and the AU are separate files. On macOS 14 (Sonoma) and earlier you can instead Control-click (or right-click) the app in the Finder, choose **Open** and then **Open** again; macOS 15 removed this shortcut.

**Terminal (quicker for plugins):** remove the quarantine mark that macOS put on the downloaded files. It affects only the files you name, so check the path before pressing Return:

```sh
sudo xattr -dr com.apple.quarantine "/Applications/NL2X Editor.app"
sudo xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/VST3/NL2X Editor.vst3"
sudo xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/Components/NL2X Editor.component"
```

(Without `sudo` for copies under your home folder.) Alternatively, run `xattr -d com.apple.quarantine` on the downloaded zip **before** you unzip it: then nothing inside it is quarantined. If macOS ever says the editor "is damaged and can't be opened", that is the same quarantine block, and the commands above fix it. Only do this for files downloaded from this project's Releases page.

## MIDI setup

The editor talks to the synth directly, in both directions, so it needs both MIDI cables:

- the interface's MIDI Out to the synth's MIDI In, and
- the synth's MIDI Out to the interface's MIDI In.

Choose the MIDI input and output ports **inside the editor** (Settings), not in the DAW; it remembers them. Only one editor at a time should control the synth: if a second plugin instance or the standalone app is already connected, the editor warns you and offers Take control.

**Channels.** The synth uses two kinds of MIDI channel, and the editor reads both from the synth:

- **Slot Channels**: each Slot A–D receives and sends notes, Program Changes and knob movements on its own channel. Set them on the synth with **Shift + MIDI CH**, then press the Slot button and use UP/DOWN (or `oFF`). Give each Slot its own channel, for example A = 1, B = 2, C = 3, D = 4. The Performance view shows them; Slots that share a channel are flagged.
- **Global Channel**: the channel of the synth's SysEx (its device ID) and of Performance changes. Press **Shift + MIDI CH**, then **MIDI CH** again: the display shows `G.x`. Set it to a channel that no Slot uses, for example 16, as the manual advises. The editor finds it by itself (Settings > Global Channel: Automatic); if it cannot, choose it there by hand.

A Slot whose channel is `oFF`, or shared with another Slot or the Global Channel, still shows its sound, but the editor cannot select its Programs, and the Store into another Bank asks you to switch the lit Slot by hand.

The plugin produces no audio: its stereo output is silent. Notes still come from a normal MIDI track in your DAW sent to the synth, as without the plugin, and you hear the synth through your audio interface as usual.

When the synth is connected, the editor runs a **Connection check** and shows the result. If something is wrong (a missing cable, a setting on the synth), it says what to change. You can run it again from the header bar.

## Synth settings

On the synth, hold **Shift** and press **PROG/CTRL** repeatedly to step through its three settings, and use UP/DOWN to set each:

- **P** (Program Change) **on**: needed by the Bank/Program selectors, the Librarian's Store and Send, and project recall.
- **C** (Control Change) **on**: the editor edits parameters with control changes, so nothing follows with C off.
- **A** (Arpeggio MIDI Out) **off**, so that arpeggios do not loop back.

**Local** is On after every power-up, which is right unless you route the synth's keyboard through your DAW with Local Off.

**Writing to the synth's memory.** The Nord Lead 2X has no memory-protect switch: any Program of the RAM Banks 0–3 can be overwritten by a received dump, as the editor's Store and Send do. Banks 4–9 are ROM and are never written. A Store writes into the Bank of the **lit Slot** (the one whose button is lit), whatever Bank the message names, so the editor switches the lit Slot to the target Bank for the write and puts it back afterwards, unsaved edits included. When it cannot (the lit Slot's channel is off or shared, or a Kit is on it), it shows numbered steps to do on the panel. Do not play or touch the synth while a write is running.

## Safety

- **Back up first.** Before the first write, open the Librarian and save each RAM Bank you care about with "Save Bank as .syx..." into your Library. The factory Banks are also on the [Nord legacy downloads page](https://www.nordkeyboards.com/legacy-products/nord-lead-2x/downloads/).
- **Automatic backups.** Before every write the editor reads the Bank it is about to change and saves it as a `.syx` file in `~/Documents/Nord Lead 2X/Backups/`. If no backup can be made, it asks before writing anything. Backups appear in the Library under a closed "Backups" heading; to restore one, open it in the Librarian with "Replace with Bank file..." and send the Bank.
- **Read back.** After every write the editor reads the written locations back and says how many match ("Read back: 99 of 99").
- **Audition stores nothing.** Clicking a sound in the Library or in a Bank only plays it on a Slot; only Store and Send write to memory, and both ask first.
- **Project recall** sends sounds to the Slots, never to the Banks.

## Your files

- **Library folder:** `~/Documents/Nord Lead 2X/` by default (`~` is your home folder); choose another in Settings. Put your `.syx` files there, in subfolders if you like; each subfolder becomes a group. Your names, tags and favourites are kept in `library.json` in that folder.
- **Backups:** `~/Documents/Nord Lead 2X/Backups/`, whatever the Library folder is.
- **Settings** (MIDI ports, Global Channel, window size): `~/Library/Application Support/NL2X Editor/NL2X Editor.settings`.

## Using it in Ableton Live and Cubase

Insert NL2X Editor on a MIDI track as an instrument (in Live: drag it from Plug-Ins > VST3 or Audio Units onto a MIDI track; in Cubase: Add Instrument Track or Rack Instrument, then choose it). Open its window and choose the MIDI ports in Settings. Send your notes to the synth from that track's MIDI output or from a second MIDI track. The parameters appear in the DAW's automation lists with names such as "A: Filter Cutoff", and your project remembers the synth's sounds.

## Reporting problems

Open an issue on [GitHub Issues](https://github.com/andreastandalone/nl2x-editor/issues) with: the editor's version (About), your macOS version and Mac (Intel or Apple Silicon), the DAW and its version (or the standalone app), the format (VST3 or AU), your MIDI interface, what you did, what you expected and what happened. For a write, the text of the task window can be selected and copied into the issue.

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

Options: `--view A|B|C|D|performance|librarian` (the tab shown; the Librarian is shown with the simulated synth's Banks read and a few locations to send), `--bank <0-3>` (the Bank the Librarian shows), `--morph` (Morph mode on), `--settings` (the Settings panel open), `--about` (the About panel open), `--check` (the Connection check run and shown), `--kit A|B|C|D` (that Slot plays a Percussion Kit), `--offline` (the synth answers nothing), `--edge-cases` (Slots A and B on one MIDI channel, values outside the documented ranges and Morph Amounts past the ends of the ranges in Slot A), `--width <px>` (the window resized as by its corner, the aspect ratio kept), `--scale <n>` (pixel density, 1 to 4), `--dialog reading|store|manual|writing|done|failed` (instead of the editor, the Librarian's task window of a Store of Slot A to Bank 3, Program 1, while the lit Slot A is on Bank 0, in one of its phases: `reading` the backup, the confirmation (`store` when the editor switches the lit Slot, `manual` when Slot A's MIDI channel is off and the user sets the Bank by hand), `writing` after Overwrite, `done` the outcome, or `failed`, the outcome when the synth ignores the switch), `--work <dir>`.

The code layout and the rules every change follows are in [CODING_STANDARDS.md](CODING_STANDARDS.md).

## Documentation

- [Glossary](GLOSSARY.md): the terms used throughout the project.
- [Decisions](docs/adr/): why the project is built the way it is.
- [Hardware checklist](docs/hardware-checklist.md) and [release process](docs/release-process.md): how a release is checked and published.
- [MIDI/SysEx research](docs/research/nl2x-midi-research.md): what we know about the synth's protocol, with sources.

The official Nord Lead 2X manual and factory banks are not included; download them from the [Nord legacy downloads page](https://www.nordkeyboards.com/legacy-products/nord-lead-2x/downloads/).

## License and trademarks

NL2X Editor is free software under the [GNU AGPLv3](LICENSE), with no warranty. It is built with [JUCE](https://juce.com), used under the AGPLv3.

Nord, Nord Lead and Clavia are trademarks of Clavia DMI AB. NL2X Editor is an independent project, not affiliated with or endorsed by Clavia DMI AB. VST is a trademark of Steinberg Media Technologies GmbH.
