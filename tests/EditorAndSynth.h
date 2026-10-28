#pragma once

// The engine and the simulated synth, cabled both ways, driven as the plugin shell drives them:
// messages from the synth are queued and delivered on the next 10 ms timer tick, followed by
// advanceTime(). (ReadSlotsTest.cpp has an earlier copy of this rig.)

#include "MidiCable.h"
#include "SimulatedNordLead2X.h"

#include "nl2x/Engine.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace nl2x::test {

struct EditorAndSynth {
    using Bytes = std::vector<std::uint8_t>;

    // The shell's timer interval.
    static constexpr TimeMs kTickMs = 10;

    // `switches`: a reading of the unverified hardware switches other than HardwareAssumptions.h's,
    // followed by both the engine and the synth.
    explicit EditorAndSynth(hardware::Switches switches = {})
        : synth(switches),
          engine(toSynth, switches)
    {
        toSynth.plugInto([this](auto message) {
            sendTimes.push_back(now);
            synth.receive(message, now);
        });
        fromSynth.plugInto([this](auto message) { pending.emplace_back(message.begin(), message.end()); });
        synth.connectMidiOut(&fromSynth);
        for (auto slot : kAllSlots)
            synth.setEditBuffer(slot, someProgram(static_cast<int>(slotIndex(slot)) + 1));
    }

    static SimulatedNordLead2X::Program someProgram(int seed)
    {
        SimulatedNordLead2X::Program program {};
        for (std::size_t i = 0; i < program.size(); ++i)
            program[i] = static_cast<std::int8_t>(static_cast<std::uint8_t>(i * 7 + static_cast<std::size_t>(seed) * 31));
        return program;
    }

    void tick()
    {
        now += kTickMs;
        while (! pending.empty()) {
            const auto message = pending.front();
            pending.pop_front();
            engine.handleIncomingMidi(message, now);
        }
        engine.advanceTime(now);
    }

    void runFor(TimeMs durationMs)
    {
        const auto end = now + durationMs;
        while (now < end)
            tick();
    }

    bool busy() const
    {
        const auto status = engine.connectionStatus();
        return status == ConnectionStatus::SearchingGlobalChannel || status == ConnectionStatus::ReadingSlots
               || engine.performanceStatus() == PerformanceStatus::Reading;
    }

    void runUntilSettled()
    {
        for (int i = 0; i < 10000 && busy(); ++i)
            tick();
        REQUIRE_FALSE(busy());
    }

    void connect(std::optional<GlobalChannel> remembered = std::nullopt)
    {
        engine.connect(remembered, now);
        runUntilSettled();
    }

    // The messages sent to the synth that are Program dumps (any type).
    std::vector<Bytes> programDumpsSent() const
    {
        std::vector<Bytes> dumps;
        for (const auto& message : toSynth.messages())
            if (parseSysEx(message).kind == SysExKind::ProgramDump)
                dumps.push_back(message);
        return dumps;
    }

    // The messages sent to the synth that are Performance dumps.
    std::vector<Bytes> performanceDumpsSent() const
    {
        std::vector<Bytes> dumps;
        for (const auto& message : toSynth.messages())
            if (parseSysEx(message).kind == SysExKind::PerformanceDump)
                dumps.push_back(message);
        return dumps;
    }

    MidiCable toSynth;
    MidiCable fromSynth;
    SimulatedNordLead2X synth;
    Engine engine;
    std::deque<Bytes> pending;
    std::vector<TimeMs> sendTimes;
    TimeMs now = 1000;
};

} // namespace nl2x::test
