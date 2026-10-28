# Open questions

Kept by the agents working while Andrea is away. Two lists: decisions taken on his behalf, and things only he can do.

## Decisions taken on Andrea's behalf

| Issue | Question | Choice | Alternatives | Cost of changing |
|---|---|---|---|---|
| #2 | JUCE version | JUCE 9.0.4 via FetchContent | JUCE 8.0.12 | Low: one line in CMake, maybe small API fixes |
| #2 | Engine dependencies | Plain C++20, no JUCE in the engine | Engine on `juce_core` | High once many modules exist |
| #2 | Test framework | Catch2 v3 | GoogleTest, JUCE UnitTest | Medium |
| #2 | Slot Channels before the Performance is read | 1, 2, 3, 4 (`HardwareAssumptions.h`) | Factory-Performance majority 1, 1, 3, 4 | Low: one constant; #5 reads the real ones |
| #2 | Audio output of the Instrument | One silent stereo output bus | No output bus at all | Low, but changes the plugin's bus layout in saved projects |
| #2 | Plugin identity | Manufacturer code `Astd`, plugin code `Nl2x`, company "andreastandalone", bundle ID `io.github.andreastandalone.nl2x-editor` | Other codes | High after release: hosts identify the plugin by these codes |
| #2 | Minimum macOS | macOS 11 | 10.15 (JUCE allows 10.11) | Low |
| #2 | DAW parameter IDs | `A_filterCutoff` (Slot letter + stable key) | Numeric IDs | High after release: automation refers to them |
| #3 | Where the probe code lives | `probe/`, a plain C++20 library (`nl2x::probe`) under the engine's rules; the JUCE window only wires it up | Inside `engine/`; logic in the JUCE window | Low: move a folder |
| #3 | Probe in the shipped plugin | Compiled into every format, Probe button shown only when the wrapper is Standalone | A separate debug-only app target | Low |
| #3 | Classifying incoming SysEx | By message size only (139 / 1063 / 715); the type byte is reported but never trusted | Classify by type byte | Medium once #4 builds on it |
| #3 | Global Channel display | The synth shows dev 0 as Global Channel 1 (`kShownGlobalChannelOfDev0`, from [SEC-3]); plan step 2 checks it | Display 0–15 | Low: one constant |
| #3 | Bank numbering | Manual's SysEx Bank 1–4 = Banks shown as 0–3 (`kShownBankOfSysExBank1`, inference); destructive test on the Bank shown as 3 | Manual Bank N = shown Bank N | Low: one constant, checked in plan step 5 |
| #3 | Bank write safety in the probe | Write and Restore refuse without a complete backup of the same Bank (same request type) from this app session; the backup is saved at once as `.syx` in `~/Documents/Nord Lead 2X/Backups/`; Restore rewrites all 99 Programs and verifies them | Restore only Program 99 | Low |
| #3 | Simulated Bank requests | The simulator answers Bank requests only when a test chooses the request type, since the real one is unknown | Assume the printed types 11–14 | Low |
| #3 | Probe timing | A 1 ms message-thread timer drives the probe; the pacing test reports the spacing it actually achieved | A high-resolution timer thread (would break the single-thread rule) | Low |
| #4 | Kit or Program in a Slot | Decided by the size of the reply to the type-14 request (139 = Program, 1063 = Kit), never by its header; `classifySlotEditBufferReply` in `HardwareAssumptions.h` | Trust the type/spec bytes | Low: one function |
| #4 | Which Slot answers a type-14 request | The requested one (`kSlotEditBufferReplyFollowsRequestedSlot = true`); [SEC-1] suggests the active Slot matters, plan step 3 settles it | Follow the active Slot | Medium: reading four Slots would need another strategy (Performance dump, #5) |
| #4 | Request timeout and pacing | 1000 ms per reply, 20 ms gap between requests (`kRequestReplyTimeoutMs`, `kRequestSpacingMs`); probing 16 silent channels takes about 16 s | Shorter timeout (a Kit reply needs about 340 ms on the wire) | Low: two constants |
| #4 | Global Channel probe order | Remembered channel, then the one found this session, then 15 (factory), then 0–14; the probe asks for Slot A and keeps its reply as Slot A's state | Ascending 0–15 | Low |
| #4 | When the plugin connects | When both ports are open and the pair differs from the last connected one (start-up, port choice, replug); "Read from synth" probes first if no channel is known | A Connect button | Low |
| #4 | Where the found Global Channel is remembered | Per user, key `globalChannel` in the same settings file as the MIDI ports | Per project | Low |
| #4 | Parameter table | One entry per offset (66), in offset order; base parameters point to their Morph Amount and back; CC only on Filter Cutoff for now (#6 adds the rest); offset 52 (sync, ring mod, distortion bits) kept as one raw value until #6 maps CC35/CC80 | Split offset 52 into two entries now | Low before #6 |
| #4 | Model after a failed read | Values read earlier are kept and the status becomes "no answer" | Mark the Slot unknown | Low |
| #4 | Kit data | Not kept in the Synth model (only "plays a Kit"): [SEC-1] says the reply may be the stored Kit, not its Edit Buffer | Keep the Kit dump | Low |
| — | Where the agents keep their notes | `docs/agents/wiki/` in the repository | Outside the repo | Low: move the folder |

## Andrea needed

Do these in order; each item names its issue.

1. **#2, DAW check.** Load NL2X Editor (VST3) on a MIDI track in Ableton Live and in Cubase; choose the Fireface MIDI ports in its settings; turn and automate "A: Filter Cutoff" and hear the synth follow; reopen the plugin and check the ports are remembered. The AU passes `auval` in CI; optionally load it in Logic or Live too.
2. **#3, hardware session (about two hours, the most important item).** Follow `docs/hardware-discovery-plan.md` with the Probe window of the standalone app (`build/.../Standalone/NL2X Editor.app`, button **Probe**). Save the log after each step and commit the logs under `docs/research/hardware-logs/`. Everything after #3 rests on assumptions in `engine/include/nl2x/HardwareAssumptions.h` until this is done.
3. **#4, read the Slots.** With the synth connected (MIDI in and out), open the standalone app or the plugin and choose the ports: the status should read "Connected, Global Channel N" with N as shown by Shift+MIDI CH (pressed twice), and Slots A–D should list the values of the four Programs. Change a knob on the synth that sends no CC (or turn CCs off), press "Read from synth" and check the value changes. Put a Percussion Kit on one Slot and check that Slot shows "Slot X is playing a Percussion Kit". Also note whether the values of a Slot change depending on which Slot is active on the synth ([SEC-1]; plan step 3).
