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

## Added with the probe (#3), 2026-10-28

- **SysEx basics in the engine** (`engine/include/nl2x/SysEx.h`): framing, nibble coding, request and dump builders, and `parseSysEx`, which classifies by size only. Data that is not valid nibbles keeps its kind but has no decoded values, so nothing is silently masked. Every message type code is a named constant in `HardwareAssumptions.h` with source and status; #4 builds on these.
- **The probe is a separate plain C++ library**, `probe/` (`nl2x::probe`), with the engine's seam: `ProbeSession` takes a `MidiOutput`, `handleIncomingMidi()` and `advanceTime()`. Each script is a small state machine (`probe/src/Scripts.cpp`) ending with one `SUMMARY` line. Tests drive it against the simulator with a simulated 1 ms clock; synth replies are queued and delivered on the next tick, as in the app.
- **The Probe window** (`plugin/ProbeWindow.*`) is thin: fields, buttons, log. It sees all MIDI through `NL2XEditorProcessor::MidiMonitor`; every send to the synth now goes through `NL2XEditorProcessor::sendToSynth`. The port menus moved into `MidiPortSelector`, and `MidiPorts` became a `juce::ChangeBroadcaster` so that several selectors stay in step.
- **Simulator** gained the Slot Edit Buffer, All Controllers and Performance requests, Edit Buffer and Bank dumps, opt-in Bank requests (`setBankRequestTypeOfSysExBank1`), and the misbehaviours "no SysEx" and "loses SysEx sent within N ms of the previous one".

## Added with reading the Slots (#4), 2026-10-28

- **Program codec** (`engine/include/nl2x/Program.h`): `Program` (66 signed bytes), `PercussionKit` (8 Programs), and `ProgramDump` / `PercussionKitDump`, which keep the header bytes so a dump written back is byte-identical. `splitSysExMessages` splits `.syx` files. `FactoryBankTest` round-trips the official files when present (path in the CMake cache variable `NL2X_FACTORY_BANK_DIR`, default `docs/research/factory-bank/`); all 396 Programs and 40 Kits pass.
- **Parameter table** covers all 66 offsets in offset order (the enumerator's value is the offset; identity outside the engine is the `key`). `morphAmount` / `morphedParameter` pair the 25 Morph Amounts with their base parameters, ready for #6 (CCs) and #7 (Morph editing).
- **Synth connection** is private to the engine (`engine/src/SynthConnection.*`): a request queue, one request waiting at a time, reply or timeout via `advanceTime`, then a gap. A reply is credited to the waiting request if its `Accepts` predicate takes it (by size, never by header). Requests and their follow-ups are chained through `Done` callbacks.
- **Synth model** is read through `Engine::slotState(Slot)`, `globalChannel()`, `connectionStatus()` (types in `SynthState.h`); the shell polls `stateRevision()` instead of subscribing, matching the parameter polling of #2. User actions take a time argument (`connect`, `readSlot`) so that timeouts never start from a stale clock.
- **Shell**: `NL2XEditorProcessor` connects when both ports are open and the pair changed, and saves the found Global Channel through `MidiPorts::rememberGlobalChannel`. `SlotView` is the generic list (Slot buttons, 66 values with documented ranges, out-of-range values marked, "Read from synth", status). The Probe window still sees all MIDI; replies to the probe are ignored by the engine unless it is itself waiting for a reply at that moment.
