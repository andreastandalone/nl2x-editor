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
The user's collection of Programs, Percussion Kits, Performances and Banks kept as files on the computer, outside the synth.
_Avoid_: Browser, patch folder

**Audition**:
Sending a Program from the Library to a Slot's Edit Buffer to hear it, without storing it in any Bank.
_Avoid_: Preview, load
