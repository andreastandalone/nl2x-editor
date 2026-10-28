# The plugin talks to the synth through its own MIDI port, not through the DAW

The plugin opens the MIDI interface connected to the Nord Lead 2X directly and sends both CCs and SysEx there, instead of emitting MIDI on the DAW track. Ableton Live and Cubase (VST3) do not pass SysEx out of a plugin, and SysEx is the only way to reach the Velocity/Morph amounts, ring modulation and every Program/Performance/Bank transfer; routing through the DAW would have limited the editor to the CC-reachable subset. Notes are still played from a normal DAW MIDI track.

## Consequences

- The user picks the MIDI input and output ports inside the plugin's settings, independently of the DAW's MIDI setup.
- Plugin automation reaches the synth only while the plugin is running and its port is open; a frozen or bounced track does not drive the hardware.
