# Architecture decisions

On 2026-10-28 the coordinating agent fixed these points before the first line of code (ticket #2), following spec #1 ("Engine and shell are separate", "One seam: the MIDI byte boundary").

- **Engine is plain C++20 without JUCE.** Spec #1 only requires no GUI/wrapper dependency; going further keeps tests fast (no JUCE build for `ctest`) and helps the Windows port the spec mentions.
- **One input, one output.** `Engine::handleIncomingMidi(message, timeMs)` and a `MidiOutput` interface the shell implements (and the tests connect to the simulator). Complete messages, never byte fragments: the shell's MIDI layer already frames messages.
- **Time is injected.** Throttling (≈10 dumps/s per Slot), request timeouts and SysEx pacing are driven by `Engine::advanceTime(timeMs)`, called by a shell timer and by tests. Tests are deterministic and never sleep.
- **Single-threaded engine.** The shell queues MIDI input from the CoreMIDI thread and delivers it on the message thread.
- **Unverified hardware facts in one header**, `engine/include/nl2x/HardwareAssumptions.h`, shared by engine and simulator, so hardware discovery (#3) changes one file. Trade-off: a wrong assumption cannot be caught by the simulator, only on hardware; accepted because the simulator's job is to encode what we believe.
- **Plugin shell** = JUCE `AudioProcessor`, Instrument type, no audio output, VST3 + AU + Standalone, Universal. It opens the MIDI ports itself (ADR 0001).

See [toolchain.md](toolchain.md) for versions.

## Added with the walking skeleton (#2), 2026-10-28

- **The shell polls DAW parameters** on its 10 ms message-thread timer and calls `Engine::setParameter` only when a value changed, instead of listening for changes. Hosts may set parameters on the audio thread; polling keeps every engine call on the message thread without a queue, and coalesces automation bursts. When #6 makes the synth move DAW parameters, the engine must ignore the echo of a value it already holds.
- **The simulator has its own CC map**, written from research §2, rather than reusing the engine's parameter table, so a mistake in the table shows up as a failing test. Both still share `HardwareAssumptions.h`.
- **MIDI port choice** is stored per user in `~/Library/Application Support/NL2X Editor/NL2X Editor.settings` (identifier plus name, since CoreMIDI identifiers can change when an interface is replugged).
