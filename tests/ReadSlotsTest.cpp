// Connecting to the synth: finding the Global Channel, reading the four Slots' Edit Buffers,
// "Read from synth" and Slots playing a Percussion Kit. The engine is driven as the shell drives
// it and wired to the simulated synth; tests look only at the MIDI it sends and its public state.

#include "MidiCable.h"
#include "SimulatedNordLead2X.h"

#include "nl2x/Engine.h"
#include "nl2x/HardwareAssumptions.h"
#include "nl2x/Program.h"
#include "nl2x/SysEx.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <algorithm>
#include <cstdint>
#include <deque>
#include <set>
#include <vector>

using namespace nl2x;
using nl2x::test::MidiCable;
using nl2x::test::SimulatedNordLead2X;

namespace {

using Bytes = std::vector<std::uint8_t>;

// The shell's timer interval.
constexpr TimeMs kTickMs = 10;

SimulatedNordLead2X::Program someProgram(int seed)
{
    SimulatedNordLead2X::Program program {};
    for (std::size_t i = 0; i < program.size(); ++i)
        program[i] = static_cast<std::int8_t>(static_cast<std::uint8_t>(i * 7 + static_cast<std::size_t>(seed) * 31));
    return program;
}

// The engine and the simulated synth, cabled both ways. As in the plugin, messages from the synth
// are queued and delivered on the next timer tick, followed by advanceTime().
struct EditorAndSynth {
    EditorAndSynth()
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
        // Generous: 16 timeouts plus the four reads.
        for (int i = 0; i < 10000 && busy(); ++i)
            tick();
        REQUIRE_FALSE(busy());
    }

    void connect(std::optional<GlobalChannel> remembered = std::nullopt)
    {
        engine.connect(remembered, now);
        runUntilSettled();
    }

    std::optional<Program> engineProgram(Slot slot) const { return engine.slotState(slot).program; }
    static Program simulatedProgram(const SimulatedNordLead2X::Program& values) { return Program(values); }

    MidiCable toSynth;
    MidiCable fromSynth;
    SimulatedNordLead2X synth;
    Engine engine { toSynth };
    std::deque<Bytes> pending;
    std::vector<TimeMs> sendTimes;
    TimeMs now = 1000;
};

} // namespace

TEST_CASE("Connecting finds the Global Channel the synth answers on, whichever of the 16 it is")
{
    const auto channel = static_cast<GlobalChannel>(GENERATE(range(0, 16)));
    EditorAndSynth rig;
    rig.synth.setGlobalChannel(channel);

    rig.connect();

    CHECK(rig.engine.globalChannel() == channel);
    CHECK(rig.engine.connectionStatus() == ConnectionStatus::Connected);

    // Each candidate is asked once for Slot A, until the right one answers; after that only the
    // right one is used, for Slots B–D and the Performance.
    const auto& sent = rig.toSynth.messages();
    REQUIRE(sent.size() >= 5);
    std::set<GlobalChannel> asked;
    for (std::size_t i = 0; i + 4 < sent.size(); ++i) {
        CHECK(sent[i] == slotEditBufferRequest(sent[i][2], Slot::A));
        CHECK(asked.insert(sent[i][2]).second);
    }
    CHECK(sent[sent.size() - 5] == slotEditBufferRequest(channel, Slot::A));
    CHECK(sent.back() == performanceEditBufferRequest(channel));
}

TEST_CASE("Connecting asks the remembered Global Channel first")
{
    EditorAndSynth rig;
    rig.synth.setGlobalChannel(6);

    rig.connect(GlobalChannel { 6 });

    const std::vector<Bytes> expected { slotEditBufferRequest(6, Slot::A), slotEditBufferRequest(6, Slot::B),
                                        slotEditBufferRequest(6, Slot::C), slotEditBufferRequest(6, Slot::D),
                                        performanceEditBufferRequest(6) };
    CHECK(rig.toSynth.messages() == expected);
    CHECK(rig.engine.globalChannel() == GlobalChannel { 6 });
}

TEST_CASE("Connecting with a remembered Global Channel that no longer answers still finds the right one")
{
    EditorAndSynth rig;
    rig.synth.setGlobalChannel(9);

    rig.connect(GlobalChannel { 2 });

    REQUIRE_FALSE(rig.toSynth.messages().empty());
    CHECK(rig.toSynth.messages().front() == slotEditBufferRequest(2, Slot::A));
    CHECK(rig.engine.globalChannel() == GlobalChannel { 9 });
    CHECK(rig.engine.connectionStatus() == ConnectionStatus::Connected);
}

TEST_CASE("With no answer, probing asks each Global Channel once, waits out each timeout and gives up")
{
    EditorAndSynth rig;
    rig.synth.setAnswersSysEx(false);
    const auto start = rig.now;

    rig.connect();

    CHECK(rig.engine.connectionStatus() == ConnectionStatus::NoAnswer);
    CHECK_FALSE(rig.engine.globalChannel());
    for (auto slot : kAllSlots)
        CHECK(rig.engine.slotState(slot).content == SlotContent::Unknown);

    const auto& sent = rig.toSynth.messages();
    REQUIRE(sent.size() == 16);
    std::set<GlobalChannel> asked;
    for (const auto& message : sent)
        asked.insert(message[2]);
    CHECK(asked.size() == 16);
    for (std::size_t i = 1; i < rig.sendTimes.size(); ++i)
        CHECK(rig.sendTimes[i] - rig.sendTimes[i - 1] >= hardware::kRequestReplyTimeoutMs);
    CHECK(rig.now - start >= 16 * hardware::kRequestReplyTimeoutMs);

    // Nothing more is sent afterwards.
    rig.runFor(10 * hardware::kRequestReplyTimeoutMs);
    CHECK(rig.toSynth.messages().size() == 16);
}

TEST_CASE("Connecting reads the Edit Buffers of all four Slots")
{
    EditorAndSynth rig;
    const auto revisionBefore = rig.engine.stateRevision();

    rig.connect();

    CHECK(rig.engine.stateRevision() != revisionBefore);
    for (auto slot : kAllSlots) {
        INFO("Slot " << slotLetter(slot));
        CHECK(rig.engine.slotState(slot).content == SlotContent::Program);
        CHECK(rig.engineProgram(slot) == EditorAndSynth::simulatedProgram(rig.synth.editBuffer(slot)));
    }
}

TEST_CASE("Requests go out one at a time, each after the previous reply and a pause")
{
    EditorAndSynth rig;
    rig.synth.setGlobalChannel(hardware::kFactoryGlobalChannel);

    rig.connect();

    REQUIRE(rig.sendTimes.size() == 5);
    for (std::size_t i = 1; i < rig.sendTimes.size(); ++i)
        CHECK(rig.sendTimes[i] - rig.sendTimes[i - 1] >= hardware::kRequestSpacingMs);
}

TEST_CASE("Read from synth re-reads one Slot after it changed on the synth")
{
    EditorAndSynth rig;
    rig.connect();
    const auto channel = *rig.engine.globalChannel();
    rig.toSynth.clear();

    rig.synth.setEditBuffer(Slot::B, someProgram(42));
    CHECK(rig.engineProgram(Slot::B) != EditorAndSynth::simulatedProgram(someProgram(42)));

    const auto revisionBefore = rig.engine.stateRevision();
    rig.engine.readSlot(Slot::B, rig.now);
    rig.runUntilSettled();

    const std::vector<Bytes> expected { slotEditBufferRequest(channel, Slot::B) };
    CHECK(rig.toSynth.messages() == expected);
    CHECK(rig.engineProgram(Slot::B) == EditorAndSynth::simulatedProgram(someProgram(42)));
    CHECK(rig.engine.stateRevision() != revisionBefore);
    CHECK(rig.engine.connectionStatus() == ConnectionStatus::Connected);
}

TEST_CASE("Read from synth before any connection finds the Global Channel first")
{
    EditorAndSynth rig;
    rig.synth.setGlobalChannel(3);

    rig.engine.readSlot(Slot::C, rig.now);
    rig.runUntilSettled();

    CHECK(rig.engine.globalChannel() == GlobalChannel { 3 });
    const auto& sent = rig.toSynth.messages();
    REQUIRE(sent.size() >= 2);
    CHECK(sent[sent.size() - 2] == slotEditBufferRequest(3, Slot::C));
    CHECK(sent.back() == performanceEditBufferRequest(3));
    CHECK(rig.engineProgram(Slot::C) == EditorAndSynth::simulatedProgram(rig.synth.editBuffer(Slot::C)));
    CHECK(rig.engine.slotState(Slot::A).content == SlotContent::Unknown);
}

TEST_CASE("Read from synth reports no answer when the synth stopped answering")
{
    EditorAndSynth rig;
    rig.connect();
    const auto before = rig.engineProgram(Slot::D);
    rig.synth.setAnswersSysEx(false);

    rig.engine.readSlot(Slot::D, rig.now);
    rig.runUntilSettled();

    CHECK(rig.engine.connectionStatus() == ConnectionStatus::NoAnswer);
    // What was read before is kept.
    CHECK(rig.engineProgram(Slot::D) == before);
}

TEST_CASE("A Slot playing a Percussion Kit is marked as such and has no Program values")
{
    EditorAndSynth rig;
    SimulatedNordLead2X::PercussionKit kit {};
    kit.fill(5);
    rig.synth.playPercussionKit(Slot::C, kit);

    rig.connect();

    CHECK(rig.engine.slotState(Slot::C).content == SlotContent::PercussionKit);
    CHECK_FALSE(rig.engine.slotState(Slot::C).program);
    for (auto slot : { Slot::A, Slot::B, Slot::D })
        CHECK(rig.engine.slotState(slot).content == SlotContent::Program);

    SECTION("and back to a Program once it plays one again")
    {
        rig.synth.setEditBuffer(Slot::C, someProgram(9));
        rig.engine.readSlot(Slot::C, rig.now);
        rig.runUntilSettled();

        CHECK(rig.engine.slotState(Slot::C).content == SlotContent::Program);
        CHECK(rig.engineProgram(Slot::C) == EditorAndSynth::simulatedProgram(someProgram(9)));
    }
}

TEST_CASE("Slot A playing a Percussion Kit still identifies the Global Channel")
{
    EditorAndSynth rig;
    rig.synth.setGlobalChannel(11);
    rig.synth.playPercussionKit(Slot::A, {});

    rig.connect();

    CHECK(rig.engine.globalChannel() == GlobalChannel { 11 });
    CHECK(rig.engine.slotState(Slot::A).content == SlotContent::PercussionKit);
    CHECK(rig.engine.slotState(Slot::B).content == SlotContent::Program);
}

TEST_CASE("Values read from the synth are kept even outside the documented ranges")
{
    EditorAndSynth rig;
    auto program = someProgram(0);
    program[58] = 3;    // Voice Mode, documented 0–2
    program[59] = 7;    // Mod Wheel Destination, documented 0–4
    program[0] = -128;  // Osc2 Semitones, documented 0–120
    rig.synth.setEditBuffer(Slot::A, program);

    rig.connect();

    const auto read = rig.engineProgram(Slot::A);
    REQUIRE(read);
    CHECK(read->value(ProgramParameter::VoiceMode) == 3);
    CHECK(read->value(ProgramParameter::ModWheelDestination) == 7);
    CHECK(read->value(ProgramParameter::Osc2Semitones) == -128);
}

TEST_CASE("A parameter set in the editor changes the Slot's Program in the engine's state")
{
    EditorAndSynth rig;
    rig.connect();

    rig.engine.setParameter(Slot::A, ProgramParameter::FilterCutoff, 99);

    CHECK(rig.engineProgram(Slot::A)->value(ProgramParameter::FilterCutoff) == 99);
    CHECK(rig.synth.editBuffer(Slot::A)[3] == 99);
}
