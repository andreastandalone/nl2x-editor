#pragma once

#include <cstdint>
#include <span>

namespace nl2x {

// Where the engine's MIDI goes. The plugin shell connects it to the MIDI output port the user
// chose; tests connect it to the simulated Nord Lead 2X. Always a complete message.
class MidiOutput {
public:
    virtual ~MidiOutput() = default;
    virtual void send(std::span<const std::uint8_t> message) = 0;
};

} // namespace nl2x
