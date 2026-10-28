#pragma once

#include "nl2x/MidiOutput.h"

#include <cstdint>
#include <functional>
#include <span>
#include <utility>
#include <vector>

namespace nl2x::test {

// A MIDI cable between two test participants: it delivers every message to the receiving end
// and keeps a copy, so tests can assert on exactly what crossed the MIDI boundary.
class MidiCable final : public MidiOutput {
public:
    using Receiver = std::function<void(std::span<const std::uint8_t>)>;

    void plugInto(Receiver newReceiver) { receiver = std::move(newReceiver); }

    void send(std::span<const std::uint8_t> message) override
    {
        log.emplace_back(message.begin(), message.end());
        if (receiver)
            receiver(message);
    }

    const std::vector<std::vector<std::uint8_t>>& messages() const { return log; }
    void clear() { log.clear(); }

private:
    Receiver receiver;
    std::vector<std::vector<std::uint8_t>> log;
};

} // namespace nl2x::test
