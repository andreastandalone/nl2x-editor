# User guide

How NL2X Editor works, beyond the [README](../README.md)'s setup steps. Terms follow the [glossary](../GLOSSARY.md) and the Nord Lead 2X manual.

## The editor

- **Slots.** Every sound parameter of the Programs on Slots A–D, from the plugin's panel, from the synth's own panel or from the DAW, including the Velocity/Morph Amounts the synth's panel hides (Option-drag a knob; the Amount shows as a ring around it).
- **Two-way.** Turn a knob on the synth and the plugin follows; move a control in the plugin and the synth follows.
- **DAW automation.** Every parameter is a DAW parameter, named like "A: Filter Cutoff" or "B: LFO1 Rate Morph", for automation and controller mapping. Tested with Ableton Live and Cubase.
- **Project recall.** Your DAW project remembers the four Slots' sounds and sends them to the synth when the project is opened (Settings: you can turn this off). Bank/Program selections and manual Slot Channel overrides are kept with it, and opening a project sends no Program Change. It never stores anything in the synth's Banks.
- **Selectors.** Bank and Program for each Slot (Banks 0–9, Programs 1–99 and the Kits P0–P9), and a Performance selector. The Performance view shows the synth's Performance (Slot Channels, Split, Layer).
- **Connection check.** It finds the synth's Global Channel by itself, checks both cables, SysEx and Control Change, and tells you what to change when a cable or a setting is wrong. Run it again from the header bar.
- **Library.** Your `.syx` files (single Programs, Percussion Kits, Performances and whole Banks), each subfolder a collapsible group. Click a Program to hear it at once on the selected Slot (Audition) without storing it. Your own names, tags and favourites for each sound, with filters; suggested tags and Auto-tag.
- **Librarian.** Reads the synth's four RAM Program Banks (0–3), shows them beside the Library, and lets you drag Programs into Bank locations, store a Slot or a Library Program into a location, replace a Bank with a Bank file, and send a changed or whole Bank. Every write is backed up first and read back afterwards.
- **Undo and redo** of your edits (also Cmd+Z and Cmd+Shift+Z in the plugin window; some hosts keep these keys for their own undo).
- **Saving** a Slot, a Performance, a Library entry or a Bank as standard `.syx` files that other tools understand.
- **Window** resizable by its corner from 75 % to 200 %, with fixed sizes in the menu next to the name.
- **Standalone app** with the same editor, and a **Probe** window for hardware discovery (for developers).

## Installing

- macOS asks for your administrator password when you copy into `/Library` or `/Applications`. To install for your user account only, use the same folders under your home folder: `~/Library/Audio/Plug-Ins/VST3/` and `~/Library/Audio/Plug-Ins/Components/`.
- Live and Cubase use the VST3; Logic uses the AU.
- In Live, if the plugin does not appear, turn on Settings > Plug-Ins > Use VST3 Plug-In System Folders (and Use Audio Units).

### Why macOS blocks it the first time

The build is free and open source, so it is not signed with a paid Apple Developer ID and not notarized by Apple. macOS therefore blocks it the first time with a message such as "Apple could not verify "NL2X Editor" is free of malware". The source code is public and you can build it yourself ([development.md](development.md)).

- The **Open Anyway** button in Privacy & Security appears only for about an hour after the blocked attempt.
- The app, the VST3 and the AU are separate files: allow each one you use.
- On macOS 14 (Sonoma) and earlier you can instead Control-click (or right-click) the app in the Finder, choose **Open** and then **Open** again; macOS 15 removed this shortcut.
- The Terminal commands remove the quarantine mark macOS puts on downloaded files. They affect only the files you name, so check the path before pressing Return. Use them without `sudo` for copies under your home folder:

  ```sh
  sudo xattr -dr com.apple.quarantine "/Applications/NL2X Editor.app"
  sudo xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/VST3/NL2X Editor.vst3"
  sudo xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/Components/NL2X Editor.component"
  ```

- Alternatively, run `xattr -d com.apple.quarantine` on the downloaded zip **before** you unzip it: then nothing inside it is quarantined.
- If macOS ever says the editor "is damaged and can't be opened", that is the same quarantine block, and the commands above fix it. Only do this for files downloaded from this project's Releases page.

## MIDI

The editor talks to the synth directly, in both directions, so it needs both MIDI cables. Choose the MIDI ports **inside the editor** (Settings), not in the DAW; it remembers them. Only one editor at a time should control the synth: if a second plugin instance or the standalone app is already connected, the editor warns you and offers Take control.

**Channels.** The synth uses two kinds of MIDI channel, and the editor reads both from the synth:

- **Slot Channels**: each Slot A–D receives and sends notes, Program Changes and knob movements on its own channel (Shift + MIDI CH, the Slot button, UP/DOWN or `oFF`). The Performance view shows them; Slots that share a channel are flagged.
- **Global Channel**: the channel of the synth's SysEx (its device ID) and of Performance changes (Shift + MIDI CH, then MIDI CH again: `G.x`). The manual advises a channel no Slot uses. The editor finds it by itself (Settings > Global Channel: Automatic); if it cannot, choose it there by hand.

A Slot whose channel is `oFF`, or shared with another Slot or the Global Channel, still shows its sound, but the editor cannot select its Programs, and the Store into another Bank asks you to switch the lit Slot by hand.

The plugin produces no audio: its stereo output is silent. Notes still come from a normal MIDI track in your DAW sent to the synth, as without the plugin, and you hear the synth through your audio interface as usual.

## Synth settings

Shift + PROG/CTRL steps through the synth's three MIDI settings:

- **P** (Program Change) **on**: needed by the Bank/Program selectors, the Librarian's Store and Send, and project recall.
- **C** (Control Change) **on**: the editor edits parameters with control changes, so nothing follows with C off.
- **A** (Arpeggio MIDI Out) **off**, so that arpeggios do not loop back.

**Local** is On after every power-up, which is right unless you route the synth's keyboard through your DAW with Local Off.

## Writing to the synth's memory

The Nord Lead 2X has no memory-protect switch: any Program of the RAM Banks 0–3 can be overwritten by a received dump, as the editor's Store and Send do. Banks 4–9 are ROM and are never written. A Store writes into the Bank of the **lit Slot** (the one whose button is lit), whatever Bank the message names, so the editor switches the lit Slot to the target Bank for the write and puts it back afterwards, unsaved edits included. When it cannot (the lit Slot's channel is off or shared, a Kit is on it, or a Performance was just auditioned from the Library), it shows numbered steps to do on the panel. Do not play or touch the synth while a write is running. The text of the task window can be selected and copied, for example into an issue.

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

Besides what the README asks for, the synth's settings (P, C, Global Channel, Slot Channels) help, and for a write the copied text of the task window. Please do not attach Clavia's factory Banks or manuals.
