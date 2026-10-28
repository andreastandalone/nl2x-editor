# Hardware checklist for release v0.1

Run on Andrea's Nord Lead 2X, with the release build installed as in the README, in **Ableton Live** and in **Cubase**. The standalone app is checked once.

**The hardware discovery session ([hardware-discovery-plan.md](hardware-discovery-plan.md)) comes first.** Until it is done and its results are in `engine/include/nl2x/HardwareAssumptions.h`, several expected results below are assumptions. Where an item says "note", write the observation in the margin or in an issue; a note that contradicts an assumption means the constant must be corrected before release.

How to read this: a plain checkbox is run once (standalone app or either DAW). Items with two boxes, `Live [ ]  Cubase [ ]`, are run in both DAWs. Terms follow the glossary.

Starting state for every section unless stated otherwise: synth connected with both MIDI cables, **Shift + PROG/CTRL: P on, C on, A off**, Local On, Performance mode off, Slot A active, a normal Program (not a Percussion Kit).

## 1. Setup and connection

- [ ] Install the release zip as in the README on a Mac where the build has never run. Opening it the first time is blocked by macOS; **Open Anyway** (System Settings > Privacy & Security) lets it open. Expected: the plugin loads afterwards.
- [ ] Standalone app: it opens, the settings list the Fireface MIDI ports. Choose them. Quit and reopen: the ports are remembered.
- Load NL2X Editor (VST3) on a MIDI track, choose the Fireface ports in its settings.
  - Live [ ]  Cubase [ ]  Turn and automate "A: Filter Cutoff": the synth follows.
  - Live [ ]  Cubase [ ]  Close the plugin window and reopen it, then save and reopen the project: the ports are remembered.
- [ ] Optional: the AU loads in Live (and Logic if available). `auval` passes in CI.
- [ ] Notes from a normal MIDI track routed to the synth sound as before; the plugin adds no audio and no noise.
- Plugin sizes and instances:
  - Live [ ]  Cubase [ ]  Two instances: the second warns and does not move the synth; delete the first, press Take control: the second works.
  - [ ] The standalone app next to the DAW warns "another application".

## 2. Connection check

- [ ] First use with the synth connected: the check opens by itself, five OK lines, indicator "Connected, Global Channel N" with N equal to G.N (Shift + MIDI CH, then MIDI CH).
- [ ] Unplug the synth's MIDI OUT, press Connection check, play a key when asked (after about 16 s): "return cable" problem.
- [ ] Cables back. Unplug only the synth's MIDI IN, run the check, play a key: SysEx problem; MIDI from the synth is OK.
- [ ] Turn C off (Shift + PROG/CTRL until C, DOWN to oF), run the check, turn a knob when asked: both CC tests show the C instruction. Note whether the knob prompt appears at all (tells whether the All Controllers Request still answers with C off). Turn C on again.
- [ ] Change the synth's Global Channel so it differs from the editor's manual choice: "Global Channel" problem naming the synth's channel. With Automatic it simply connects.
- [ ] Switch the synth off while connected: indicator red "Not answering", the check opens. Switch on again and recover.
- [ ] Standalone app, run a Probe script: indicator "Paused while the Probe runs".

## 3. Reading the Slots

- [ ] With the synth connected, the status reads "Connected, Global Channel N" (N as on the synth) and Slots A-D list the values of the four Programs.
- [ ] Change a knob on the synth that sends no CC (or turn C off), press "Read from synth": the value changes in the plugin.
- [ ] Put a Percussion Kit on one Slot: that Slot shows "Slot X is playing a Percussion Kit".
- [ ] Note whether a Slot's values depend on which Slot is active on the synth (plan step 3).

## 4. Performance and Slot Channels

- [ ] The Performance tab says the Performance was read; the Slot Channels match Shift + MIDI CH on each Slot, including "off".
- [ ] Set Slot B to off on the synth, press the tab's "Read from synth": B shows off.
- [ ] Put A and B on the same channel: the main view warns "Slots A and B share MIDI channel N". Layer A + C and read again: the Layer warning appears.
- [ ] Change Split Point and Keyboard Split in the tab: the synth follows and nothing else changes (Master Tune and Global Channel in particular).
  - Live [ ]  Cubase [ ]  Automate "Performance: Split Point": the synth follows.
- [ ] Note whether the type-30 Performance dump applies at once.
- [ ] Set a manual Slot Channel override on one Slot, save and reopen the project: the override is kept.

## 5. CC parameters, two-way

Synth with C on.

- Live [ ]  Cubase [ ]  The parameter list shows "A: Gain" ... "D: LFO2 Dest/Arp Mode"; choices have names, switches are on/off.
- Live [ ]  Cubase [ ]  Transport stopped, move "B: LFO1 Rate": the synth follows on Slot B.
- [ ] In the Slots tab change a slider, a menu and a toggle: the synth follows.
- Live [ ]  Cubase [ ]  Turn knobs and press buttons on the synth, on each Slot: the plugin follows.
- Live [ ]  Cubase [ ]  Arm automation write (Live: Arm + record; Cubase: W) and move a parameter: the movement is recorded, with no jumping back and no feedback.
- [ ] For every stepped button, step through it on the synth and note the CC value next to the LED shown (Probe "Watch CCs" logs them). Record the CC35 value of each Ring Mod/Sync state, the value each switch sends for on, and the CC78 values at the Semitones extremes and centre (plan step 9). Correct the option orders in `HardwareAssumptions.h` if they differ.
- [ ] Send Unison on from the plugin: the synth turns it on.
- [ ] With Sync on, toggle Distortion from the plugin: Sync stays on.

## 6. Morph Amounts

- [ ] On Slot A playing a Program, Option-drag a knob: the violet arc appears, and playing harder or moving the mod wheel (destination Morph) sounds different. Note whether the type-0 dump applies at once, whether notes are cut, and whether the Slot keeps its edited state (research §7.4).
- [ ] Drag a Morph Amount fast for a few seconds: in the Probe log the dumps are about 100 ms apart; nothing lost or hung.
- [ ] Step the RING MOD/SYNC button and note the CC35 values; if Ring Mod never shows up in CC35, set `kRingModReachableByCc35 = false`.
- [ ] A Slot playing a Percussion Kit shows its notice and cannot be edited.
- Live [ ]  Cubase [ ]  "A: Filter Cutoff Morph" ... are listed (-128...127); automation recorded from Option-drags and the Morph button plays back slowly and the synth follows.
- [ ] Change a Morph Amount on the synth (VELOCITY/MORPH ASSIGN), press "Read from synth": it shows in the plugin.

## 7. Selectors (Bank/Program and Performance)

With P on and C on.

- [ ] Slot A selector Bank 1, Program 23: the synth shows 1.23; after about 0.2 s the Slot A tab shows the new values.
- [ ] P3 in Bank 0 for Slot B: the synth plays Kit P3 and Slot B shows the Kit notice (Program Change 99-108).
- [ ] Bank 4, Program 99 (top of the range) works.
- [ ] Performance Bank 2, C5: the synth shows it; the Performance tab and Slot Channels update; then "A: Filter Cutoff" follows on the new channel.
- [ ] A Performance in Bank 4 and L9 (tests Bank - 1 and 99 = L9).
- [ ] Turn the Program dial on the synth: the selector follows and the values refresh. Note whether CC32 arrives too (Probe log).
- [ ] Turn the Performance dial: note whether the synth transmits on the Global Channel and whether the plugin re-reads everything.
- [ ] A and B on the same channel, select from A: note whether B switches too. Repeat with A + C layered.
- [ ] C off, select a Bank from the plugin: note whether CC32 is still honoured. Turn C on again.
- Live [ ]  Cubase [ ]  Save and reopen the project: the selectors show the saved selection and no Program Change goes out.
- [ ] If the values shown after a selection are the old ones, `kSelectionSettleMs` must grow.
- [ ] Note what Performance offsets 328 and 332 hold for Banks 4-9 and for Kits.

## 8. Library and Audition

- [ ] Copy the factory `bank0.syx` and `Perf0.syx` into `~/Documents/Nord Lead 2X/`, open the plugin, press Refresh: bank0 opens into "Bank 0 - 1...99" and "P0...P9"; Perf0 into "A0...L9".
- [ ] Select Slot B in the Slots tab, click "Bank 0 - 5": the sound changes at once and no Program in the synth's Bank 0 changed (Audition).
- [ ] On a Slot playing a Percussion Kit click a Program: refused with a reason. Click a Kit entry: the synth plays it.
- [ ] Double-click a Performance: it plays and the Slot Channels update.
- [ ] "Save Slot as .syx" and "Save Performance as .syx", then send each file with SysEx Librarian: the Slot file plays on its Slot; the Performance file loads into the Performance Edit Buffer.
- [ ] Select a Kit inside bank0, "Save entry as .syx", send it to a Slot playing a Kit.
- [ ] Choose another folder, restart the plugin: the folder is remembered.

## 9. Writing to the synth's memory

Do this after the hardware session. Use Bank 3 as the test Bank and back it up first with the Probe.

- [ ] Plan step 5 identifies the request type that reads each Bank; `kBankRequestTypeOfSysExBank1` is set, and only after plan step 11 restores correctly is `kBankReadingVerified = true`.
- [ ] Synth memory tab, Bank 3, "Read Bank from synth...": the file appears in the Library as a Program Bank with 99 entries and matches the Probe backup.
- [ ] Select Bank 0 on the synth, then "Store Slot A to Bank/Program..." with Bank 3, Program 99: after the confirmation it says "Bank 0 is selected on the synth, not Bank 3" and writes nothing.
- [ ] Select a Program in Bank 3 on the synth, store again: Program 99 of Bank 3 changes and a backup appears in `~/Documents/Nord Lead 2X/Backups/`.
- [ ] "Send Bank file to synth...": backup first, then the question, about 10 s of writing and "Read back: 99 of 99". If fewer match, raise `kBankWriteSpacingMs`.
- [ ] While the backup folder is locked, the second "No backup possible" dialog appears and Cancel writes nothing.
- [ ] Restore the synth's Bank 3 from your own backup at the end.

## 10. Project recall

Synth connected, P and C on.

- Live [ ]  Cubase [ ]  Change A: Filter Cutoff, a Morph Amount on Slot B and the Split Point; save; change the sounds on the synth; close and reopen the project. Expected: the Slots and the Split Point come back; a MIDI monitor or the Probe log shows one 715-byte and four 139-byte messages and no CC on open.
- Live [ ]  Cubase [ ]  Save a device preset (`.adv` in Live, `.vstpreset` in Cubase), change the sounds, load the preset. Expected: sent at once.
- Live [ ]  Cubase [ ]  Slot Channel overrides and the saved Bank/Program selection survive the reopen, and no Program Change goes out on open.
- [ ] Untick "Send the project's sounds to the synth when the project opens" and reopen. Expected: nothing is sent, the views show the synth's sounds; the setting survives a restart.
- [ ] Open the project with the synth switched off, then switch it on. Expected: the sounds are sent as soon as the synth is found; the DAW never hangs.
- [ ] Check whether the Performance dump alone restores the Slots (if so, set `kProjectRecallSendsSlotDumpsAfterPerformance = false`), what a Slot playing a Percussion Kit does with a Performance dump, and that no Bank changed.

## 11. Undo and redo

- Live [ ]  Cubase [ ]  Drag a knob, press Undo: the synth and the DAW parameter both go back, no echo, the drag is one step. Redo.
- Live [ ]  Cubase [ ]  The same with a Morph Amount (Option-drag) and with a Performance edit (Split Point or Layer).
- [ ] Cmd+Z with the plugin window focused: Live [ ]  Cubase [ ]  standalone [ ]. Note which hosts swallow it.
- Live [ ]  Cubase [ ]  The DAW's own undo of plugin parameter changes does not conflict with the plugin's.
- Live [ ]  Cubase [ ]  With automation writing on, an undo is recorded as a parameter move.

## 12. Look and feel

The colours are placeholders until they are sampled from a photo of the synth (issue #13); they live only in `plugin/NordLookAndFeel.h` (entries marked PLACEHOLDER).

- [ ] Colours sampled from the photo and put into `plugin/NordLookAndFeel.h`.
- [ ] Panel layout approved by Andrea in Ableton Live (#13). Decision / date: ____________
- [ ] Every control sits in its section where the synth has it (LFO 1, LFO 2/Arp, Mod Env left; Oscillators centre; Amplifier, Filter right; play controls along the bottom). Notes: ____________
- [ ] The order of the LED choices matches the synth's LEDs (it follows `HardwareAssumptions.h` until the hardware session settles it). Notes: ____________
- [ ] Colours and violet Morph arc readable on a dark and a light DAW theme. Notes: ____________
- [ ] The fixed 1378 × 758 window fits and reads well in Live [ ]  Cubase [ ]  standalone [ ].
- [ ] Text is legible at the default size; no clipped labels. Notes: ____________
- [ ] About window shows the trademark notice ("Not affiliated with or endorsed by Clavia DMI AB. Nord and Nord Lead are trademarks of Clavia DMI AB.") and the version.

## Result

Date: ____________  Build: ____________  Live version: ____________  Cubase version: ____________

- [ ] All items pass, or each exception is recorded as an issue and judged acceptable for 0.1.
