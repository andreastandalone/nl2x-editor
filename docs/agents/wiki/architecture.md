# Architecture decisions

On 2026-10-28 the coordinating agent fixed these points before the first line of code (ticket #2), following spec #1 ("Engine and shell are separate", "One seam: the MIDI byte boundary").

- **Engine is plain C++20 without JUCE.** Spec #1 only requires no GUI/wrapper dependency; going further keeps tests fast (no JUCE build for `ctest`) and helps the Windows port the spec mentions.
- **One input, one output.** `Engine::handleIncomingMidi(message, timeMs)` and a `MidiOutput` interface the shell implements (and the tests connect to the simulator). Complete messages, never byte fragments: the shell's MIDI layer already frames messages.
- **Time is injected.** Throttling (≈10 dumps/s per Slot), request timeouts and SysEx pacing are driven by `Engine::advanceTime(timeMs)`, called by a shell timer and by tests. Tests are deterministic and never sleep.
- **Single-threaded engine.** The shell queues MIDI input from the CoreMIDI thread and delivers it on the message thread.
- **Unverified hardware facts in one header**, `engine/include/nl2x/HardwareAssumptions.h`, shared by engine and simulator, so hardware discovery (#3) changes one file. Trade-off: a wrong assumption cannot be caught by the simulator, only on hardware; accepted because the simulator's job is to encode what we believe.
- **Plugin shell** = JUCE `AudioProcessor`, Instrument type, no audio output, VST3 + AU + Standalone, Universal. It opens the MIDI ports itself (ADR 0001).

See [toolchain.md](toolchain.md) for versions.
