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
| — | Where the agents keep their notes | `docs/agents/wiki/` in the repository | Outside the repo | Low: move the folder |

## Andrea needed

(none yet)
