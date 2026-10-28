# Hardware discovery plan

A step-by-step session at the Nord Lead 2X to settle what the manual gets wrong or leaves out (issue #3). Every step uses the **Probe** window of the NL2X Editor standalone app and ends with **Save log**. Findings go into `docs/research/nl2x-midi-research.md` (tag them **[HW]** with the date) and into `engine/include/nl2x/HardwareAssumptions.h`, which the engine and the simulated synth both read.

Budget about two hours. Nothing in this plan writes to the synth's Banks except step 11, which is optional, starts with a backup, and asks for confirmation before each write.

## The Probe window

- **Ports**: the same two menus as the main window (MIDI Input from the synth, MIDI Output to the synth); a change in one window shows in the other.
- **Fields**: *Global Channel (1–16)*, *CC sweep: CC number*, *CC sweep: channel (1–16)*, *Bank request type (from step 5)*, *Bank dump type (1–10)*. Scripts read them when they start.
- **Script buttons**: one per script below. Hover a button for what it sends. The two scripts that write to the synth's memory are red, and each asks "Write to the synth's memory?" before it starts.
- **Stop** ends the running script; it still prints its summary. Only one script runs at a time. The label next to Stop shows *Running…* or *Idle*.
- **Raw entry + Send**: type any one MIDI message in hex (`F0 33 0F 04 0E 00 F7`, `B0 4A 40`) and press Send or Return. Anything that is not one complete, valid message is refused with a note in the log.
- **Log**, **Clear log**, **Save log**: Save log offers `~/Documents/nl2x-probe-<date>-<time>.txt`.

Closing the Probe window only hides it; reopening it keeps the log and the session (including a Bank backup).

### Reading the log

One line per event, fields separated by spaces:

```
+12345.6 ms  OUT   F0 33 0F 04 0E 00 F7
+12391.0 ms  IN    F0 33 0F 04 00 00 … F7
+12391.0 ms  NOTE  SysEx in: 139 bytes, Program dump, dev 15, type 0, spec 0
```

- The time is in milliseconds since the window opened or the log was last cleared. `IN` times are the MIDI driver's arrival times.
- `OUT` and `IN` lines hold every byte, including MIDI the editor sends from its own knob.
- `NOTE` lines come from the probe:
  - `START <script>` when a script begins;
  - `SysEx in: <size> bytes, <kind>, dev <d>, type <t>, spec <s>` for every SysEx received (kind is Program dump for 139 bytes, Percussion Kit dump for 1063, Performance dump for 715, otherwise "other Nord Lead message"; "data bytes are not nibbles" marks a corrupt dump);
  - `SUMMARY <script>: …`, the last line of every script, with what it found;
  - `Not started: <script>: <reason>` when a script refuses to run.

"dev" is the Global Channel byte in the SysEx header, 0–15. The probe assumes the synth shows dev 0 as Global Channel 1 (`kShownGlobalChannelOfDev0`); step 2 checks it.

## Before you start

1. Cable the Fireface UFX MIDI Out to the synth's MIDI In **and** the synth's MIDI Out to the Fireface MIDI In.
2. On the synth, **Shift + PROG/CTRL**: turn **P** (Program Change) on, **C** (Control Change) on, **A** (Arp MIDI Out) off.
3. Read the Global Channel: **Shift + MIDI CH** pressed twice shows `G.x`. Write it down.
4. Note each Slot's MIDI channel (Shift + MIDI CH on each Slot) and write them down.
5. Performance mode off (plain Program mode), Slot A active alone, a normal Program (not a Percussion Kit) on every Slot.
6. Start the NL2X Editor standalone app, press **Probe**, choose the Fireface ports in the Probe window, type the Global Channel from item 3 into *Global Channel (1–16)*, press **Clear log**.

Each step below names the button to press, what to do on the synth, and what to write down.

## 1. Basic link

- Press **All Controllers Request A**: an All Controllers Request for Slot A (`F0 33 <dev> 04 14 00 F7`) to every dev 0–15, 300 ms apart (about 5 s). CCs arriving in the 300 ms after a request are credited to it.
- Expect a burst of CCs from the synth after the dev that matches the Global Channel. The summary reads like `SUMMARY All Controllers Request A: 1 of 16 requests answered: dev 15 -> 38 CCs on channel 1`.
- Write down: which dev answered; number of CCs; their channel.

## 2. Global Channel probing

- Press **Probe Global Channel**: a Slot A Edit Buffer request (type 14, spec 0) to every dev 0–15, 300 ms apart.
- When exactly one dev answers, the summary ends with `Adopted Global Channel <n> (dev <d>)` and the *Global Channel* field changes to it. Otherwise it says how many devs answered and leaves the field alone.
- Write down: which dev answered, that no other dev produced a reply, and whether `<n>` equals the `G.x` from "Before you start" item 3 (this checks the display assumption).

## 3. Slot Edit Buffer request

- Press **Request Slot A–D (type 14)**, then **Request Slot A–D (type 10)**. Each sends spec 0–3 (Slot A–D), one second apart; the summary lists the reply to each, e.g. `Slot B (type 14 spec 1) -> 139 bytes, Program dump, dev 15, type 0, spec 1`.
- Repeat with only Slot B active, then with Slot C playing a Percussion Kit.
- Write down for each: reply size (139 bytes for a Program, 1063 for a Kit), type/spec of the reply, and whether the reply follows the requested Slot or the active Slot (compare the `IN` bytes between Slots).
- Turn the Filter Cutoff knob on Slot A to a new position, press **Request Slot A–D (type 14)** again: does Slot A's dump show the new value (Edit Buffer) or the stored one? Filter Cutoff is the 4th data value: bytes 13 and 14 of the message, counting `F0` as byte 1, low nibble first.

## 4. Percussion Kit Edit Buffer request

- With Slot A on a Percussion Kit, press **Request Kit Slot A (type 10, spec 16)**, **Request Kit Slot A (type 14, spec 16)** and **Request Kit Slot A (type 14, spec 0)** (one request each, 1.5 s to answer).
- Change one percussion sound on the synth without storing, press them again: Edit Buffer or stored Kit?

## 5. Bank location requests (read-only, safe)

- Press **Scan Bank requests**: types 11–24 × specs 0, 1, 98, 99, 108, one request every 400 ms (70 requests, about 28 s). Nothing is written.
- The summary lists every (type, spec) that got an answer, with the reply's size and header. Two families of hits are already known and are not Bank requests: type 14 (the Slot Edit Buffer request) and type 20 (the All Controllers Request, answered with CCs).
- Write down every other hit. Spec 0, 1 and 98 address Programs 1, 2 and 99; spec 99 and 108 address Percussion Kits P0 and P9 (expect 1063 bytes).
- **Which Bank is which.** The synth shows Program Banks 0–9, of which 0–3 are RAM [OFF-2X p.13]. The manual's SysEx tables count Banks from 1: Program dump types 1–4 and the printed request types 11–14 are "Bank 1–4". So the manual's Bank 1 is presumably the Bank the synth shows as 0, and its Bank 4 the Bank shown as 3 (`kShownBankOfSysExBank1`, an inference). To check a hit: on the synth load Program 1 of Bank 0 into Slot A, press **Request Slot A–D (type 14)**, and compare Slot A's reply bytes with the hit's reply bytes in the log; repeat with Bank 3.
- Write down the request type that reads the Bank shown as **3**: step 11 needs it.

## 6. Performance

- Switch to Performance mode, select a RAM Performance.
- Press **Request Performance Edit Buffer (type 40)** and **Request Performance Bank 1 location 0 (type 41)** (one request each).
- Write down: reply size (expect 715), type/spec.
- Change the split on the synth without storing, press **Request Performance Edit Buffer (type 40)** again: Edit Buffer or stored?

## 7. Program dump to an Edit Buffer

- Back in Program mode, Slot A active with a sustained sound. Hold a note (or use the sustain pedal).
- Press **Send Program to Slot A: cutoff low/high**. It reads Slot A's Program, sends it back to Slot A's Edit Buffer (type 0, spec 0) with Filter Cutoff 0, then 3 s later with Filter Cutoff 127; nothing else changes. The log notes each dump (`Sending Slot A Program with Filter Cutoff 0`).
- Write down: does the sound change immediately, while the note is held? After the next note? Does the display change?
- Press **Send Program to Slot A: Morph Amount on Cutoff** (Filter Cutoff 20 and a Morph Amount of +100 on Filter Cutoff), then play softly and hard: is the morph audible?
- Press **Restore Slot A Program**: it sends back the Program read before the first of these scripts changed Slot A.

## 8. Performance dump to the Edit Buffer

- In Performance mode, press **Send Performance: toggle split**: it reads the Performance Edit Buffer (type 40) and sends it back (type 30) with Keyboard Split switched. The summary says what the split was and what was sent.
- Write down: immediate? Slot channels unchanged? Any notes cut?
- Press it again to switch the split back.

## 9. CC values sent by the synth

- Press **Watch CCs**, operate the synth as listed below, then press **Stop**. For each CC seen the log prints a `CC range:` line: up to 16 different values are listed one by one (`CC35 on channel 1: 4 values: 0 1 2 3`), more are summarised (`CC78 on channel 1: 61 values from 0 to 120, steps 2`). Write down which value belongs to which panel state while you operate the synth; the `IN` lines keep the order.
  - Switches: Unison, Osc2 Kbd Track, Filter Velocity, Portamento Auto, Filter Distortion: value sent for on and for off.
  - **Ring Mod/Sync button** (CC35): press it through all four states, value for each state in panel order.
  - **Osc2 Semitones** (CC78): turn from minimum to maximum slowly.
  - Stepped controls, step through every option from the lowest: Mod Wheel Destination (CC18), LFO1 Waveform (CC20), LFO1 Destination (CC21), LFO2 Dest/Arp (CC24), Mod Env Destination (CC28), Filter Type (CC44), Filter Kbd Track (CC46), Osc1/Osc2 Waveform (CC30/31), Play Mode (CC15), Oct Shift (CC17). For each value write the LED/display shown.
- Then the reverse direction: type 35 into *CC sweep: CC number* and Slot A's channel into *CC sweep: channel*, press **Send CC sweep** (values 0–127, 250 ms apart, about 32 s), and note what the panel shows; repeat with 78. Does the synth accept the same values it sends?

## 10. SysEx pacing

- Slot A on a normal Program. Press **Pacing test**: it reads Slot A, then for each spacing 0, 5, 10, 20 and 50 ms sends 20 Program dumps to Slot A's Edit Buffer (only Filter Cutoff differs between them), each followed by a read-back request and a comparison. The spacing is the gap between handing the dump and the request to the MIDI port, and between a reply and the next dump. At the end it sends the original Program back.
- After each spacing the log prints `Pacing <s> ms (measured <min>–<max> ms): <n> of 20 read-backs match: PASS` (or FAIL); the summary repeats PASS/FAIL per spacing and names the smallest spacing where every read-back matched. A spacing that fails waits one second per lost reply, so the test takes 10 s to 2 minutes.
- Write down the summary. Note: a 139-byte dump takes about 45 ms on the MIDI cable itself, so at short spacings the messages queue in the interface; the result tells what the whole chain (app, Fireface, synth) tolerates. The probe's clock ticks every millisecond but macOS may stretch that, hence the measured spacing.

## 11. Optional: Bank write (writes to the synth's memory)

Only with a Bank you can afford to lose; skip the whole step if step 5 found no Bank request. The test uses the last RAM Bank, the one the synth shows as **Bank 3** (the manual's SysEx Bank 4), and overwrites only its Program 99.

- On the synth, select any Program in **Bank 3**: the synth stores incoming Bank dumps in the Bank selected on its panel, whatever the dump's type byte says [OFF-2X p.78].
- Type the request type that reads Bank 3 (from step 5) into *Bank request type*. Leave *Bank dump type* at 4 (the manual's Bank 4).
- Press **Back up Bank** (reads only): it requests Programs 1–99 of that Bank one by one (about 10 s). The summary reads `captured all 99 Programs with request type <t>; reply type bytes: …; reply spec equals the location for 99 of 99`, and a note gives the file the backup was saved to, `~/Documents/Nord Lead 2X/Backups/probe-bank-backup-<date>-<time>-request-type-<t>.syx` (the replies exactly as received). If any Program does not answer, no backup is kept and the next two buttons refuse to run.
- Press **Write test Program to Bank (writes to memory)** and confirm. It sends one Program dump to Program 99 (dump type 4, spec 98): the backed-up Program 99 with Filter Cutoff changed. After 500 ms it reads Program 99 back; the summary says whether the test Program was stored there, whether Program 99 is unchanged, or whether it differs from both.
- Write down: did it store? Check on the synth too: select Bank 3, Program 99. If the summary says "unchanged", look for the change elsewhere (Program 99 of the Bank shown as 0, or of whichever Bank was selected).
- Press **Restore Bank backup (writes to memory)** and confirm. It sends all 99 backed-up Programs into the selected Bank, 100 ms apart (`kBankWriteSpacingMs`), then reads each back; the summary ends `read-back: <n> of 99 match the backup`. Anything below 99 means 100 ms is too fast for Bank writes: send the saved backup file again with a slower tool and write the spacing down.

Both writing scripts refuse to run (`Not started: …`) unless a complete backup of the Bank read with the same *Bank request type* was captured since the app started.

## 12. Sharing the MIDI output port with the DAW

- Open Ableton Live with a MIDI track sending notes and a CC automation to the Fireface output, playing in a loop.
- Slot A on a normal Program. In the Probe, press **Stress: Program dumps**: it reads Slot A and sends that same Program back to Slot A's Edit Buffer every 50 ms for 30 s (600 dumps), then reads Slot A again. The summary gives the number of messages and SysEx received meanwhile, how many were malformed, and whether Slot A's Program read back unchanged.
- Write down: any stuck notes, wrong sounds, or errors on the synth display, and the summary.

## After the session

Save the log after each step (one file per step is easiest to read), then put the files in `docs/research/hardware-logs/` (our own logs, not third-party material, so they can be committed) or send them to the agent. Keep the Bank backup file outside the repository. The agent updates the research document, `HardwareAssumptions.h` and the simulated synth, and closes #3.
