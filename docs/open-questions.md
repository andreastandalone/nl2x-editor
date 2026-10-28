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
| — | Where the agents keep their notes | `docs/agents/wiki/` in the repository | Outside the repo | Low: move the folder |

## Andrea needed

Do these in order; each item names its issue.

1. **#2, DAW check.** Load NL2X Editor (VST3) on a MIDI track in Ableton Live and in Cubase; choose the Fireface MIDI ports in its settings; turn and automate "A: Filter Cutoff" and hear the synth follow; reopen the plugin and check the ports are remembered. The AU passes `auval` in CI; optionally load it in Logic or Live too.
