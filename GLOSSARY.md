# Nord Lead 2X Editor

A DAW plugin that edits and manages the sounds of a Clavia Nord Lead 2X synthesizer. Terms follow the official Nord Lead 2X manual; the plugin's user interface is in English and uses these same words.

## Language

**Slot**:
One of the four parts A, B, C and D of the synth, each playing its own Program or Percussion Kit, together or separately.
_Avoid_: Bank (when meaning A–D), part, layer, channel

**Program**:
A single sound: the complete set of sound parameters played by one Slot.
_Avoid_: Patch, preset, sound

**Percussion Kit**:
A group of eight Programs played as one drum sound, each assigned to its own key range.
_Avoid_: Drum kit, kit program

**Performance**:
A snapshot of all four Slots, each with a full copy of its Program, plus the settings that tie them together (layers, split, MIDI channels).
_Avoid_: Combi, multi, setup, preset

**Bank**:
A numbered area of the synth's memory holding stored Programs, Percussion Kits or Performances.
_Avoid_: Library, folder

**Morph Amount**:
How far a Program parameter moves away from its base value as velocity or the modulation wheel increases; set per parameter, with no dedicated knob on the synth.
_Avoid_: Velocity amount, modulation depth

**Slot Channel**:
The MIDI channel a Slot listens and transmits on. It is part of the Performance, so it can change when a different Performance is selected.
_Avoid_: Part channel

**Global Channel**:
The synth-wide MIDI channel used for Performance changes; it also serves as the synth's SysEx Device ID.
_Avoid_: Device ID (as a separate setting), base channel

**Edit Buffer**:
The Program, Percussion Kit or Performance a Slot (or the synth) is currently playing, including unsaved changes, as opposed to the copy stored in a Bank.
_Avoid_: Current patch, working copy

**Library**:
The user's collection of Programs, Percussion Kits, Performances and Banks kept as files on the computer, outside the synth. Each subfolder of the Library folder is a **group** named after it (for example `Factory`, `My sounds`); the `Backups` subfolder holds Bank backups and is listed apart.
_Avoid_: Browser, patch folder

**Suggested category**:
A category the Library guesses for a sound that has none (Percussion for a Percussion Kit, a keyword in the file name, or Bass or Keys from clear parameters), shown apart from the user's own categories and stored only when the user keeps it.
_Avoid_: Auto-tag, automatic category

**Audition**:
Sending a Program from the Library to a Slot's Edit Buffer to hear it, without storing it in any Bank.
_Avoid_: Preview, load

**Librarian**:
The part of the editor that shows the Library beside the synth's RAM Program Banks (0–3) as last read, and puts Library Programs into Bank locations.
_Avoid_: Bank manager, organizer

**Composed Bank**:
The Librarian's version of one RAM Bank: the synth's Bank (or a Bank file) with the locations the user changed, which stay pending ("to send") until they are written to the synth.
_Avoid_: Working copy, draft Bank

**Connection check**:
The editor's test of the MIDI link with the synth: whether MIDI arrives from it, whether it answers SysEx requests and on which Global Channel, and whether it transmits and receives CCs (the C setting under Shift + PROG/CTRL). Each problem comes with the setting to change.
_Avoid_: Diagnostics, self-test
