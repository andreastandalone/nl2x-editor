# Clavia Nord Lead 2X: MIDI / SysEx implementation research

Date of research: 2026-10-28.

## Sources and tags

Every fact below carries one of these tags:

- **[OFF-2X p.N]**: official *Nord Lead 2X User Manual, OS v1.0x, Edition 1.1* (English), printed page N. In the PDF, page index = printed page + 2.
  - URL: https://www.nordkeyboards.com/wt/documents/218/Nord%20Lead%202x%20English%20User%20Manual%20v1.0%20Edition%201.1.pdf
  - Local copy (not in git, download it from the URL above): `docs/research/NordLead2X_UserManual_EN_v1.0_Ed1.1.pdf`
- **[OFF-NL2 p.N]**: official *Nord Lead 2 English User Manual v1.0*, printed page N.
  - URL: https://www.nordkeyboards.com/wt/documents/217/Nord%20Lead%202%20English%20User%20Manual%20v1.0.pdf
  - Local copy (not in git): `docs/research/NordLead2_UserManual_EN_v1.0.pdf`
- **[OFF-FB]**: official *Nord Lead 2X Factory Bank v1.00 revA* (.syx and .mid files), which I decoded myself.
  - URL: https://www.nordkeyboards.com/wt/documents/797/Nord%20Lead%202x%20Factory%20Bank%20v1.00%20revA.zip
  - Local copy (not in git, unzip it from the URL above): `docs/research/factory-bank/`
  - Analysis scripts: `docs/research/tools/syx.py` and `docs/research/tools/ranges.py`
- **[OFF-DL]**: Nord legacy downloads page, https://www.nordkeyboards.com/legacy-products/nord-lead-2x/downloads/
- **[SEC-x]**: secondary sources (forums, third-party code), listed in section 6.
- **[INF]**: my own inference. Verify on hardware.

---

## 1. Architecture

### Slots and channels

| Topic | Fact | Source |
|---|---|---|
| Slots | 4 Program Slots, A–D. Each Slot is an edit buffer. Several Slots can be active at once (layer). Keyboard split puts A/B on the lower part and C/D on the upper part. | [OFF-2X p.14–16] |
| Polyphony | NL2X: 20 voices. NL2: not checked here; the NL2 manual p.105 mentions "a 4-voice Nord Lead 2", so polyphony varied by unit. | [OFF-2X p.15]; [OFF-NL2 p.105] |
| Slot MIDI channel | Each Slot has its own MIDI channel, 1–16 or "oFF" (set with Shift+MIDI CH). A Slot receives and transmits notes, CCs and Program Changes on its channel. | [OFF-2X p.67, 75–76] |
| Which channel is transmitted | The NL2X transmits on the channel of the *active* (selected) Slot. | [OFF-2X p.76] |
| Reception | All four Slots always receive. Slots in a layer **also** receive on the channel of the leftmost active Slot. For independent multitimbral control, keep only one Slot active. | [OFF-2X p.76–77] |
| Same channel on several Slots | All Slots on that channel react to CCs. | [OFF-2X p.77] |
| Global MIDI channel | Used for Performance Program Change / Bank Select, and it is **the SysEx Device ID** (0–15). It should differ from all Slot channels. If it equals a Slot channel, Performance selection wins. | [OFF-2X p.67, 76, 105] |
| CCs to Percussion Kits | A CC affects the percussion sound last selected for editing (the last black key pressed). | [OFF-2X p.103] |

### Program memory (NL2X)

- **Programs**: 10 banks × 99. Banks 0–3 are RAM (writable). Banks 4–9 are ROM. Displayed as `1-9.99`; bank 0 shows no prefix. [OFF-2X p.13]
- **Percussion Kits**: 4 × 10, writable (P0–P9 in banks 0–3). They sit "above" Program 99. Each kit holds 8 percussion sounds, and each sound is a full 66-parameter patch. [OFF-2X p.13, 28]
- **Performances**: 4 banks × 100, named A0–L9 (letters I and K are skipped). Bank 1 is RAM. Banks 2–4 are ROM. ROM Performances can be edited but can only be stored to the first bank. [OFF-2X p.16, 32]
- A Performance holds **copies** of the 4 Programs, so editing the source Program later does not change it. Exception: Percussion Kits are stored **by reference only**. [OFF-2X p.31–33]

### NL2 vs NL2X memory

| | Nord Lead 2 | Nord Lead 2X |
|---|---|---|
| Internal Programs | 99 (1–40 RAM, 41–99 ROM) [OFF-NL2 p.12, 101] | 10 × 99 (banks 0–3 RAM) |
| Card | PCMCIA S-RAM 64 KB: +3 × 99 Programs, +30 Percussion Kits, +100 Performances [OFF-NL2 p.12, 23] | No card; memory is internal [OFF-2X] |
| Percussion Kits | 10 internal (ROM, not overwritable), plus 30 on the card [OFF-NL2 p.12, 94] | 40 internal, all writable |
| Performances | 100 internal ROM, plus 100 on the card [OFF-NL2] | 100 RAM + 300 ROM |
| Bank Select | Received only (CC0 and CC32) [OFF-NL2 p.92, "Bank Select" section] | CC32, transmitted and received [OFF-2X p.76, 104] |

---

## 2. CC map

Source: [OFF-2X p.103–104], verified against the rendered page images. The NL2 list is identical [OFF-NL2 p.91–92].

Ranges:

- Knobs are 0–127.
- On/off buttons send 0 = off. The manual's wording for "on" is unclear; it says *a value of "on"*. [INF] Expect 1 or ≥64. Test on hardware.
- Stepped buttons send 0, 1, 2, … from the lowest setting.

The SysEx offset column comes from the dump table (section 3). Pairing a CC with a SysEx offset is [INF], but the names match one to one.

| Parameter | CC | Range / steps | SysEx offset |
|---|---|---|---|
| Gain | 7 | 0–127 | 17 |
| Osc Mix | 8 | 0–127 | 2 |
| Portamento Time | 5 | 0–127 | 16 |
| Poly/Legato/Mono | 15 | 0–2 | 58 voicemode |
| Unison | 16 | 0–1 | 60 |
| Oct Shift | 17 | 0–4 | 64 |
| Mod Wheel Destination | 18 | 0–4 (Morph, LFO1, Osc2, FM, Filter; order [INF]) | 59 |
| LFO1 Rate | 19 | 0–127 | 21 |
| LFO1 Waveform | 20 | 0–4 | 56 |
| LFO1 Destination | 21 | 0–4 | 57 |
| LFO1 Amount | 22 | 0–127 | 22 |
| LFO2/Arp Rate | 23 | 0–127 | 23 |
| LFO2 Dest / Arp Mode | 24 | 0–8 (8 = off) | 65 |
| LFO2 Amount / Arp Range | 25 | 0–127 | 24 |
| Mod Env Attack | 26 | 0–127 | 18 |
| Mod Env Decay | 27 | 0–127 | 19 |
| Mod Env Destination | 28 | 0–3 | 61 |
| Mod Env Amount | 29 | 0–127 (64 = centre) | 20 |
| Osc1 Waveform | 30 | 0–3 (3 = sine) | 50 |
| Osc2 Waveform | 31 | 0–3 (3 = noise) | 51 |
| Osc2 Fine Tune | 33 | 0–127 | 1 |
| Osc2 Keyboard Tracking | 34 | 0–1 | 54 |
| Oscillator Sync (Ring Mod/Sync button) | 35 | see note below | 52, bits 0–1 |
| Amp Env Decay | 36 | 0–127 | 13 |
| Amp Env Sustain | 37 | 0–127 | 14 |
| Filter Env Attack | 38 | 0–127 | 8 |
| Filter Env Decay | 39 | 0–127 | 9 |
| Filter Env Sustain | 40 | 0–127 | 10 |
| Filter Env Release | 41 | 0–127 | 11 |
| Filter Resonance | 42 | 0–127 | 4 |
| Filter Env Amount | 43 | 0–127 | 5 |
| Filter Mode (type) | 44 | 0–4 | 53 |
| Filter Velocity | 45 | 0–1 | 63 |
| Filter Keyboard Track | 46 | 0–3 | 55 |
| Portamento Auto | 65 | 0–1 | 62 |
| Osc FM Depth | 70 | 0–127 (also Ring Mod tune) | 7 |
| Amp Env Release | 72 | 0–127 | 15 |
| Amp Env Attack | 73 | 0–127 | 12 |
| Filter Cutoff | 74 | 0–127 | 3 |
| Osc2 Semitones | 78 | see note below | 0 (0–120, 60 = centre) |
| Osc Pulse Width | 79 | 0–127 | 6 |
| Filter Distortion | 80 | 0–1 | 52, bit 4 |

Notes on the table:

- **CC35 (Oscillator Sync)**: the panel has one Ring Mod/Sync button that cycles through off, sync, ring and both [OFF-2X p.42]. The CC values for these states are **not documented**. [INF] Probably 0–3 matching bits 0–1 of SysEx byte 52.
- **CC78 (Osc2 Semitones)**: the SysEx range is 0–120, but a CC can only carry 0–127. The CC scaling is **unknown**. Test on hardware.

Other controllers [OFF-2X p.74, 104]:

- CC1 Mod wheel: Tx/Rx.
- CC11 Expression pedal: Tx/Rx.
- CC64 Sustain: Tx.
- CC32 Bank Select: Tx/Rx.
- Pitch bend: Tx, and Rx using the bend range setting.
- Aftertouch: received only (channel pressure), not transmitted.
- Master Level: no CC.

Mod wheel and pedals are always Tx/Rx, even when the panel CC setting is off [OFF-2X p.77].

**Not reachable by CC.** Use SysEx (patch or performance dump) for these:

- All 25 **Velocity/Morph sensitivity** values (offsets 25–49, signed −128…127, 0 = off). The Morph target is set by pressing VELOCITY/MORPH ASSIGN and turning knobs. There is no CC for the morph amounts or for the assign button. The morph amount is driven by mod wheel CC1 (when the destination is Morph), by velocity, by the expression pedal, or by "External Velocity Morph". [OFF-2X p.26–27, 71, 111]
- Ring Mod as a separate control (only the combined bits in byte 52; see the CC35 note).
- All Performance-level settings:
  - Slot MIDI channels and LFO1/LFO2 MIDI clock sync.
  - Env/Morph trigger channel and note.
  - Bend range, unison detune, out mode.
  - Aftertouch and expression pedal destination/amount.
  - Split and split point, layer state.
  - Per-Slot program and bank.
- Global settings: global channel, Prog Change/CC on/off, master tune, local, arp MIDI out. Even via SysEx these are **"not received!!!"** (section 3).

---

## 3. SysEx

### General format

Source: [OFF-2X p.105], identical in [OFF-NL2 p.93].

```
F0 33 <dev> 04 <msgType> <msgSpec> [data…] F7
```

- `33` = Clavia manufacturer ID.
- `<dev>` = Global MIDI channel, 0–15.
- `04` = Model ID. Both manuals print `$04`: the NL2X manual says "Model ID for Nord Lead 2X" and the NL2 manual says "for Nord Lead".
- There is **no checksum** (none is documented).

[OFF-FB] confirms the format: every factory file starts `F0 33 0F 04 …`, with dev = 0x0F.

### Data encoding

Source: [OFF-2X p.110].

- Each parameter is 8 bits, **two's complement (signed)**.
- Each byte is **nibblized** into 2 MIDI bytes, **low nibble first**: `value = d[2i] | (d[2i+1] << 4)`, then sign-extend.
- Patch: 66 parameters → 132 data bytes. Full message = 139 bytes.
- Percussion Kit: 8 × 66 parameters → 1056 data bytes. Full message = 1063 bytes.
- Performance: 4 × 66 (Slots A–D) + 90 performance parameters = 354 parameters → 708 data bytes. Full message = 715 bytes.

All three sizes are verified against [OFF-FB]: 139, 1063 and 715 bytes, and all nibbles are ≤ 0x0F.

### Message types (NL2X as printed)

Source: [OFF-2X p.105–109].

| Message | Type byte | Spec byte | Data |
|---|---|---|---|
| Patch dump (Tx/Rx) | 0 = Edit buffer; 1–10 = Bank 1–10 | Edit buffer: 0–3 = Slot A–D. Bank: 0–98 = Program 1–99 | 132 |
| Perc Kit dump (Tx/Rx) | 0 = Edit buffer; 1–4 = Bank 1–4 | Edit buffer: 16–19 = Slot A–D. Bank: 99–108 = P0–P9 | 1056 |
| Patch/Kit dump **request** (Rx only) | 10 ($0A) = Edit buffer; 11–14 = Bank 1–4 | Edit buffer: 0–3 = Slot. Bank: 0–98 = Program, 99–108 = Kit | none |
| Performance dump (Tx/Rx) | 30 = Perf edit buffer; 31–34 = Perf Bank 1–4 | 0, or 0–99 | 708 |
| Performance dump request (Rx only) | 40 = Edit buffer; 41–44 = Perf Bank 1–4 | 0, or 0–99 | none |
| All Controllers Request (Rx only) | 20 ($14) | Slot 0–3 | none. The synth replies with **CC messages** for that Slot. |

On the Performance request row, the manual's body text says "41 or 42" while the table says 41–44.

**NL2 differences** [OFF-NL2 p.93–97]:

- Patch dump types are only 0–4.
- Request types 10–14 do not overlap with the dump types.
- Performance dump: 30 = edit buffer, 31 = PCMCIA card Performance bank.
- Performance request: 40 = edit buffer, 41 = card.
- Internal NL2 kits cannot be overwritten.

The NL2X manual copied the NL2 tables and only partly updated them.

**Known errata / field reports for the patch request**:

- The 2X manual extends patch dump types to 0–10. Request type 10 ("edit buffer") then collides with "Bank 10". [INF] This is probably why the documented request fails.
- [SEC-1], electro-music forum, saxwell, 2025-03-17, https://electro-music.org/forum/viewtopic.php?p=456577:
  - `F0 33 0F 04 0A 00 F7` (type 10) gives **no output** on a 2X.
  - **Type 14** (`F0 33 <dev> 04 0E <slot> F7`) returns the Slot's edit-buffer patch, but only if the **active Slot on the synth holds a normal Program**. Otherwise it returns an "empty" patch.
  - If the active Slot holds a Percussion Kit, any request returns a 1063-byte kit that appears to be the stored kit, not the edit buffer.
  - The reported lengths 137/1061 exclude F0/F7.
- [SEC-2], maxforlive.com device "Nord Lead 2X Remote" by Kampfer, https://maxforlive.com/library/device/1907/nord-lead-2x, quoted via search snippet:
  - "Patch Dump Request 14 (Edit Buffer) 0-3 (Slot A-D)" works on the 2X.
  - The same string does not work correctly on the original NL2.
- **Conclusion**: the bank-location request type codes on the 2X are **unknown** and need a hardware probe. [INF] Candidates: 11–14 as printed, or 15–24 if the request table was shifted by +14. Probe with types 11–24 and specs 0–108, and log the replies.
- [SEC-3], Nord User Forum thread "Nord lead 2x All Controllers Request" (https://www.norduserforum.com/viewtopic.php?p=145915, quoted via search snippet only; the forum returns 403):
  - `F0 33 00 04 14 00 F7` works on the NL2X for Slot A, with the global channel set to 1 (dev 0).

**Receive-side rules** [OFF-2X p.78, 105–107, 30]:

- A single patch with type 0 goes into that Slot's edit buffer and is not stored.
- Bank-dump patches are stored at their program number **in the bank currently selected on the synth**. The type byte's bank number is effectively ignored.
- A kit can only be received into a Slot that currently plays a kit, and a program only into a Slot playing a program. Otherwise "nothing will happen".
- A Performance dump from Performance mode holds only kit *references*. Kit data must be dumped from Program mode.

**[OFF-FB] observation**: the official factory .syx files use type 2 for "bank0", 3 for "bank1", 4 for "bank2", and 2 again for "bank3". The type byte in bank dumps is therefore unreliable. Do not trust it as a location.

### Patch layout (66 bytes, offsets in decoded parameters)

Source: [OFF-2X p.111–112] = [OFF-NL2 p.98–99].

| Off | Name | Range | Off | Name | Range |
|---|---|---|---|---|---|
| 0 | osc2pitch | 0–120 (60 = centre) | 33 | filterenvattack_sens | −128…127 |
| 1 | osc2pitchfine | 0–127 | 34 | filterenvdecay_sens | " |
| 2 | mix | 0–127 | 35 | filterenvsustain_sens | " |
| 3 | cutoff | 0–127 | 36 | filterenvrelease_sens | " |
| 4 | resonance | 0–127 | 37 | ampenvattack_sens | " |
| 5 | filterenvamt | 0–127 | 38 | ampenvdecay_sens | " |
| 6 | pw | 0–127 | 39 | ampenvsustain_sens | " |
| 7 | fmdepth | 0–127 | 40 | ampenvrelease_sens | " |
| 8 | filterenvattack | 0–127 | 41 | portamento_sens | " |
| 9 | filterenvdecay | 0–127 | 42 | gain_sens | " |
| 10 | filterenvsustain | 0–127 | 43 | modenvattack_sens | " |
| 11 | filterenvrelease | 0–127 | 44 | modenvdecay_sens | " |
| 12 | ampenvattack | 0–127 | 45 | modenvlevel_sens | " |
| 13 | ampenvdecay | 0–127 | 46 | lfo1rate_sens | " |
| 14 | ampenvsustain | 0–127 | 47 | lfo1level_sens | " |
| 15 | ampenvrelease | 0–127 | 48 | lfo2rate_sens | " |
| 16 | portamento | 0–127 | 49 | arprange_sens | " |
| 17 | gain | 0–127 | 50 | osc1waveform | 0–3 (3 = sine) |
| 18 | modenvattack | 0–127 | 51 | osc2waveform | 0–3 (3 = noise) |
| 19 | modenvdecay | 0–127 | 52 | sync/ringmod/dist | bit0 = sync, bit1 = ring mod, bit4 = filter distortion |
| 20 | modenvlevel | 0–127 (64 = centre) | 53 | filtertype | 0–4 |
| 21 | lfo1rate | 0–127 | 54 | osc2kbdtrack | 0–1 |
| 22 | lfo1level | 0–127 | 55 | filterkbdtrack | 0–3 |
| 23 | lfo2rate | 0–127 | 56 | lfo1wave | 0–4 |
| 24 | arprange | 0–127 | 57 | lfo1dest | 0–4 |
| 25 | osc2pitch_sens | −128…127 ("velocity/morf sens") | 58 | voicemode | 0–2 |
| 26 | osc2pitchfine_sens | " (0 = OFF) | 59 | modwheeldest | 0–4 |
| 27 | mix_sens | " | 60 | unison | 0–1 |
| 28 | cutoff_sens | " | 61 | modenvdest | 0–3 |
| 29 | resonance_sens | " | 62 | auto (portamento) | 0–1 |
| 30 | filterenvamt_sens | " | 63 | filtervel | 0–1 |
| 31 | pw_sens | " | 64 | octshift | 0–4 |
| 32 | fmdepth_sens | " | 65 | lfo2dest/arpmode | 0–8 (8 = off) |

[OFF-FB] range check across all 436 factory programs and kit sounds:

- Offset 52 reaches 18 (bit4 + bit1), which matches the bit layout.
- Offset 58 (voicemode) reaches 3, and offset 59 (modwheeldest) reaches 7. Both exceed the documented maxima.
- **The editor must preserve unknown or out-of-range values instead of clamping.**

### Performance layout

Source: [OFF-2X p.112] = [OFF-NL2 p.100]. Offsets are in decoded parameters. `[A–D]` means 4 consecutive bytes.

| Off | Name | Range |
|---|---|---|
| 0–263 | patches A, B, C, D (66 each) | see patch layout |
| 264 | midichan[A–D] | 0–15. [OFF-FB] also contains −1; [INF] −1 = "off". |
| 268 | lfo1sync[A–D] | 0–7 |
| 272 | lfo2sync[A–D] | 0–7 |
| 276 | filterenvtrig[A–D] | 0–1 |
| 280 | filterenvtrigmidichan[A–D] | 0–15 |
| 284 | filterenvtrignotenr[A–D] | 23–127 (23 = off / any note) |
| 288 / 292 / 296 | ampenvtrig / …midichan / …notenr | same ranges as filter env trig |
| 300 / 304 / 308 | morftrig / …midichan / …notenr (External Velocity Morph) | same ranges |
| 312 | bendrange | 0–8 |
| 313 | unisondetune | 0–8 |
| 314 | outmode | low nibble = A/B mode, high nibble = C/D mode, 0–3 |
| 315 | globalmidichan | 0–15, **not received** |
| 316 | midiprogchange | 0–1, **not received** |
| 317 | midicntrl | 0–1, **not received** |
| 318 | mastertune | −99…99, **not received** |
| 319 | pedaltype | 0–2, **not received** |
| 320 | localcontrol | 0–1, **not received** |
| 321 | keyboard octave shift | 0–4, **not received** |
| 322 | selected_channel (active Slot) | 0–3 |
| 323 | Arp MIDI Out | 0–1, **not received** |
| 324 | channel_actived[A–D] (layer on/off) | 0–1 |
| 328 | pgmselect[A–D] | 0–98 |
| 332 | bankselect[A–D] | 0–3 |
| 336 | aftertouch amount[A–D] | 0–7 |
| 340 | aftertouch dest[A–D] | 0–4 |
| 344 | expression pedal amount[A–D] | 0–7 |
| 348 | expression pedal dest[A–D] | 0–4 |
| 352 | keyboard split | 0–1 |
| 353 | splitpoint | 0–127 |

[OFF-FB] observations:

- Offset 315 = 15, which matches dev 0x0F.
- Offsets 321 and 323 hold constants (−94, 17) in every factory Performance, which suggests they are unused or garbage.
- Offset 312 values reach 14, beyond the documented 0–8.
- Offsets 308–311 contain some out-of-range note numbers.
- Again: **round-trip unknown bytes untouched.**

Code cross-references (#5): the settings table is `engine/include/nl2x/PerformanceSettings.h`; the −1 = off reading of offset 264 and the Layer reception rule (§1) are `kPerformanceSlotChannelOff` / `slotChannelFromPerformance` and `kLayeredSlotsAlsoReceiveOnLeftmostActiveSlotChannel` in `engine/include/nl2x/HardwareAssumptions.h`.

---

## 4. Gotchas

1. **Shift > PROG/CTRL** [OFF-2X p.66–67]:
   - `P` = Program Change on/off, `C` = Control Change on/off. Both are global, and each switch covers both Tx and Rx.
   - `C` must be **on** for CC editing to work. Mod wheel and pedals are always active.
   - `A` (Arp MIDI Out) should be **off** when using Local Off or a thru loop, otherwise you get silence or a MIDI feedback loop.
2. **Local** [OFF-2X p.66]:
   - The synth always powers up in Local On.
   - In Local Off, the panel knobs only send MIDI and do not change the sound, unless CCs are disabled.
   - For an editor with MIDI thru, use Local Off and make sure the editor echoes CCs back on the right channel.
3. **Power-on resets** [OFF-2X p.63]: Local resets to On and the Special functions reset to off on every power-up. The Special settings are still saved inside Performances.
4. **SysEx Device ID = Global MIDI channel** (Shift+MIDI CH pressed twice shows "G.x"). The editor needs a setting for it. Global settings in a Performance dump are not applied on receive.
5. **Selecting a Performance** changes Slot MIDI channels and can make Slots silent [OFF-2X p.33]. After a Performance change, re-read the Performance to learn the Slot channels.
6. **Slot type must match** for kit or program receive and for edit-buffer requests [OFF-2X p.30; SEC-1].
7. **Layering**: layered Slots also receive on the leftmost active Slot's channel. An editor sending per-Slot CCs should warn or force a single active Slot [OFF-2X p.76–77].
8. **Kits in Performances** are references only [OFF-2X p.31–32].
9. **Timing**: no official inter-message delay is documented. [INF]
   - The synth writes flash or RAM per received bank patch, so pace bank uploads; a starting point is ≥ 50–100 ms between 139-byte messages, then tune.
   - Wait for each reply before sending the next request.
   - Avoid sending dense CC streams while a SysEx transfer is in progress.
10. **OS versions**: the Nord downloads page lists **no OS updates. The final version is 1.00 (2003-01-01)** [OFF-DL, via search snippet]. Manual "Edition 1.1" is a document revision, not an OS version. The MIDI chart is dated 2003-04-01 [OFF-2X p.113]. No version-specific SysEx differences are documented.
11. **Nord Rack 2X**: the 2X manual covers both the keyboard and the Rack [OFF-2X p.113 chart header], so the protocol is presumably identical. [INF]

---

## 5. Open-source and other implementations

| Project | Link | Language | License | Notes |
|---|---|---|---|---|
| larme/nord-lead-viewer | https://github.com/larme/nord-lead-viewer | CoffeeScript/JS parser inside a Max patch | **None stated**, so it is all-rights-reserved by default. Read it for reference only; do not copy code. | Parses single patches (137 bytes without F0/F7, type 0 with spec 0–3, or type 1–10 with spec 0–98) and kits (1061 bytes, type 0 with spec 16–19, or type 1–4 with spec 99–108). Nibble decoding: low + 16×high, signed. Tested on a Rack 2X. |
| Ctrlr NL2X panel | https://github.com/RomanKubiak/ctrlr/issues/322 | Ctrlr/Lua | Unknown | The panel link is dead; only the issue remains. |
| "Nord Lead 2X Remote" (Max for Live) by Kampfer | https://maxforlive.com/library/device/1907/nord-lead-2x | Max | Check the page | Source of the type-14 request finding. The page returned 429 to me, so I relied on a search snippet. |
| Patch Base (Coffeeshopped) | https://coffeeshopped.com/patch-base/editor/clavia-nord-lead-2 | — | Commercial, closed | NL2 voice, performance and bank editor. Its manual says the editor can only save to NL2 internal patches 1–40. |
| reKon Audio "VST-AU Lead2 Editor" | https://www.kvraudio.com/product/vst-au-lead2-editor-by-rekon-audio | — | Commercial, closed | NL2, 2X and Rack. Reportedly does not receive dumps. |
| Mystery Islands NL2xR Editor | https://rekkerd.org/?p=98978 | — | Commercial | VST/AU for NL2/NL2X. |

I found no permissively licensed open-source NL2/NL2X editor. The official manual tables plus the official factory .syx files are enough to implement and test the format independently.

---

## 6. Secondary source list

- [SEC-1] https://electro-music.org/forum/viewtopic.php?p=456577: "Nord Lead 2X patch dump request sysex". Also cross-posted at https://www.norduserforum.com/viewtopic.php?p=181704 (403 to automated fetch).
- [SEC-2] https://maxforlive.com/library/device/1907/nord-lead-2x (search snippet only).
- [SEC-3] https://www.norduserforum.com/viewtopic.php?p=145915 (search snippet only).
- https://github.com/larme/nord-lead-viewer
- https://coffeeshopped.com/patch-base-manual/clavia-nord-lead-2

## 7. Open questions to settle on hardware

1. Request type codes for bank locations on the 2X. Is the edit-buffer request type 14 confirmed for all Slots? How do you request a kit edit buffer?
2. CC value encoding:
   - CC35 sync/ring states.
   - CC78 scaling against SysEx 0–120.
   - The "on" value for switch CCs.
   - The enumeration order for CC18 / CC20 / CC21 / CC24 / CC28 / CC44 / CC46.
3. Whether a Performance edit-buffer dump (type 30) applies immediately, and how long the synth needs to digest a 715-byte message.
4. Whether type-0 patch dumps sent to a Slot are audible immediately. This matters for "send on edit" of the morph/sens parameters that have no CC.
