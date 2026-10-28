# Open questions

Kept by the agents working while Andrea is away. Two lists: decisions taken on his behalf, and things only he can do.

## Decisions taken on Andrea's behalf

| Issue | Question | Choice | Alternatives | Cost of changing |
|---|---|---|---|---|
| #2 | JUCE version | JUCE 9.0.4 via FetchContent | JUCE 8.0.12 | Low: one line in CMake, maybe small API fixes |
| #2 | Engine dependencies | Plain C++20, no JUCE in the engine | Engine on `juce_core` | High once many modules exist |
| #2 | Test framework | Catch2 v3 | GoogleTest, JUCE UnitTest | Medium |
| — | Where the agents keep their notes | `docs/agents/wiki/` in the repository | Outside the repo | Low: move the folder |

## Andrea needed

(none yet)
