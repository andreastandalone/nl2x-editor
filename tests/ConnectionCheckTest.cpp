// The Connection check (#9), the manual Global Channel and suspending the engine's requests while
// the Probe runs. Each diagnosis is provoked by making the simulated synth misbehave; tests look
// only at the MIDI crossing the boundary and the engine's public report.

#include "MidiCable.h"
#include "SimulatedNordLead2X.h"

#include "nl2x/ConnectionCheck.h"
#include "nl2x/Engine.h"
#include "nl2x/HardwareAssumptions.h"
#include "nl2x/SysEx.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cstdint>
#include <deque>
#include <set>
#include <vector>

using namespace nl2x;
using nl2x::test::MidiCable;
using nl2x::test::SimulatedNordLead2X;
using Catch::Matchers::ContainsSubstring;

namespace {

using Bytes = std::vector<std::uint8_t>;
using Outcome = ConnectionCheckOutcome;
using Item = ConnectionCheckItem;

constexpr TimeMs kTickMs = 10;
constexpr std::uint8_t kFilterCutoffCc = 74;
constexpr std::size_t kFilterCutoffOffset = 3;

SimulatedNordLead2X::Program someProgram(int seed)
{
    SimulatedNordLead2X::Program program {};
    for (std::size_t i = 0; i < program.size(); ++i)
        program[i] = static_cast<std::int8_t>(static_cast<std::uint8_t>(i * 7 + static_cast<std::size_t>(seed) * 31));
    program[kFilterCutoffOffset] = static_cast<std::int8_t>(40 + seed);
    return program;
}

// The engine and the simulated synth cabled both ways; messages from the synth are delivered on
// the next tick, as in the plugin.
struct EditorAndSynth {
    EditorAndSynth()
    {
        toSynth.plugInto([this](auto message) { synth.receive(message, now); });
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

    const ConnectionCheckReport& check() const { return engine.connectionCheck(); }

    // Runs until the check finishes or asks the user for something.
    void runCheckUntilItStops()
    {
        for (int i = 0; i < 100000 && check().state == ConnectionCheckState::Running; ++i)
            tick();
        REQUIRE(check().state != ConnectionCheckState::Running);
    }

    void startCheck()
    {
        engine.startConnectionCheck(now);
        runCheckUntilItStops();
    }

    // The user does nothing until the interactive step gives up.
    void waitOutUserStep()
    {
        REQUIRE(check().state == ConnectionCheckState::WaitingForUser);
        for (int i = 0; i < 100000 && check().state == ConnectionCheckState::WaitingForUser; ++i)
            tick();
        runCheckUntilItStops();
    }

    void connect(std::optional<GlobalChannel> remembered = std::nullopt)
    {
        engine.connect(remembered, now);
        for (int i = 0; i < 100000; ++i) {
            const auto status = engine.connectionStatus();
            if (status != ConnectionStatus::SearchingGlobalChannel && status != ConnectionStatus::ReadingSlots)
                return;
            tick();
        }
        FAIL("connecting did not settle");
    }

    Outcome outcome(Item item) const { return check().result(item).outcome; }

    // CCs the editor sent to the synth.
    std::vector<Bytes> ccsSent() const
    {
        std::vector<Bytes> ccs;
        for (const auto& message : toSynth.messages())
            if (message.size() == 3 && (message[0] & 0xF0) == 0xB0)
                ccs.push_back(message);
        return ccs;
    }

    MidiCable toSynth;
    MidiCable fromSynth;
    SimulatedNordLead2X synth;
    Engine engine { toSynth };
    std::deque<Bytes> pending;
    TimeMs now = 1000;
};

void requireFinished(const EditorAndSynth& rig)
{
    REQUIRE(rig.check().state == ConnectionCheckState::Finished);
    CHECK(rig.check().userPrompt.empty());
}

} // namespace

TEST_CASE("Connection check: all good, every test passes without asking the user anything")
{
    EditorAndSynth rig;
    rig.synth.setGlobalChannel(4);
    const auto cutoffBefore = rig.synth.editBuffer(Slot::A)[kFilterCutoffOffset];

    rig.startCheck();

    requireFinished(rig);
    for (const auto& result : rig.check().results) {
        INFO(result.title << ": " << result.explanation);
        CHECK(result.outcome == Outcome::Passed);
        CHECK(result.fix.empty());
    }
    CHECK(rig.check().allPassed());
    CHECK(rig.check().answeringGlobalChannel == GlobalChannel { 4 });
    CHECK_THAT(rig.check().result(Item::GlobalChannel).explanation, ContainsSubstring("Global Channel 5"));

    // It asked for Slot A's controllers, then moved Slot A's Filter Cutoff on Slot A's channel
    // and put it back.
    const auto& sent = rig.toSynth.messages();
    CHECK(std::find(sent.begin(), sent.end(), allControllersRequest(4, Slot::A)) != sent.end());
    const auto channel = hardware::kDefaultSlotChannels[0];
    const auto ccs = rig.ccsSent();
    REQUIRE(ccs.size() == 2);
    CHECK(ccs[0][0] == (0xB0 | channel));
    CHECK(ccs[0][1] == kFilterCutoffCc);
    CHECK(ccs[0][2] != static_cast<std::uint8_t>(cutoffBefore));
    CHECK(ccs[1] == Bytes { static_cast<std::uint8_t>(0xB0 | channel), kFilterCutoffCc,
                            static_cast<std::uint8_t>(cutoffBefore) });
    CHECK(rig.synth.editBuffer(Slot::A)[kFilterCutoffOffset] == cutoffBefore);
}

TEST_CASE("Connection check: the synth ignores SysEx but MIDI arrives from it")
{
    EditorAndSynth rig;
    rig.synth.setAnswersSysEx(false);

    rig.startCheck();

    // No Global Channel answered: the check asks for a key.
    REQUIRE(rig.check().state == ConnectionCheckState::WaitingForUser);
    CHECK_THAT(rig.check().userPrompt, ContainsSubstring("Play a key"));
    rig.synth.playKey(Slot::A, 60);
    rig.tick();
    rig.runCheckUntilItStops();

    requireFinished(rig);
    CHECK(rig.outcome(Item::MidiFromSynth) == Outcome::Passed);
    CHECK(rig.outcome(Item::SysExAnswered) == Outcome::Failed);
    CHECK_THAT(rig.check().result(Item::SysExAnswered).fix, ContainsSubstring("MIDI IN"));
    CHECK(rig.outcome(Item::GlobalChannel) == Outcome::NotTested);
    CHECK(rig.outcome(Item::CcsTransmitted) == Outcome::NotTested);
    CHECK(rig.outcome(Item::CcsReceived) == Outcome::NotTested);
    CHECK_FALSE(rig.check().answeringGlobalChannel);
    // Every one of the 16 Global Channels was asked.
    std::set<GlobalChannel> asked;
    for (const auto& message : rig.toSynth.messages())
        asked.insert(message[2]);
    CHECK(asked.size() == 16);
}

TEST_CASE("Connection check: no MIDI arrives at all, the return cable seems to be missing")
{
    EditorAndSynth rig;
    rig.synth.connectMidiOut(nullptr);

    rig.startCheck();
    // The user plays a key, but it goes nowhere.
    rig.synth.playKey(Slot::A, 60);
    rig.waitOutUserStep();

    requireFinished(rig);
    CHECK(rig.outcome(Item::MidiFromSynth) == Outcome::Failed);
    CHECK_THAT(rig.check().result(Item::MidiFromSynth).explanation, ContainsSubstring("return cable"));
    CHECK_THAT(rig.check().result(Item::MidiFromSynth).fix, ContainsSubstring("MIDI OUT"));
    CHECK(rig.outcome(Item::SysExAnswered) == Outcome::NotTested);
    CHECK(rig.outcome(Item::CcsReceived) == Outcome::NotTested);
}

TEST_CASE("Connection check: skipping the key step leaves the return cable untested")
{
    EditorAndSynth rig;
    rig.synth.connectMidiOut(nullptr);

    rig.startCheck();
    rig.engine.skipConnectionCheckStep(rig.now);
    rig.runCheckUntilItStops();

    requireFinished(rig);
    CHECK(rig.outcome(Item::MidiFromSynth) == Outcome::NotTested);
    CHECK(rig.outcome(Item::SysExAnswered) == Outcome::Failed);
}

TEST_CASE("Connection check: the synth does not transmit CCs, so turn on C")
{
    EditorAndSynth rig;
    rig.synth.setTransmitsControlChanges(false);

    rig.startCheck();

    // No CC answered the All Controllers Request: the check asks for a knob.
    REQUIRE(rig.check().state == ConnectionCheckState::WaitingForUser);
    CHECK_THAT(rig.check().userPrompt, ContainsSubstring("Turn a knob"));
    // The knob turns, but no CC leaves the synth; the mod wheel always does, and proves nothing.
    rig.synth.turnPanelControl(Slot::A, kFilterCutoffCc, 99);
    rig.synth.turnPanelControl(Slot::A, 1, 99);
    rig.waitOutUserStep();

    requireFinished(rig);
    CHECK(rig.outcome(Item::SysExAnswered) == Outcome::Passed);
    CHECK(rig.outcome(Item::CcsTransmitted) == Outcome::Failed);
    CHECK_THAT(rig.check().result(Item::CcsTransmitted).fix, ContainsSubstring("PROG/CTRL"));
    CHECK_THAT(rig.check().result(Item::CcsTransmitted).fix, ContainsSubstring("Turn on C"));
    // Reception is tested separately and still works.
    CHECK(rig.outcome(Item::CcsReceived) == Outcome::Passed);
}

TEST_CASE("Connection check: a knob turned on the synth proves CCs are transmitted")
{
    EditorAndSynth rig;
    // As if the All Controllers Request were not answered on this synth, though knobs send CCs.
    rig.synth.setTransmitsControlChanges(false);

    rig.startCheck();
    REQUIRE(rig.check().state == ConnectionCheckState::WaitingForUser);
    rig.synth.setTransmitsControlChanges(true);
    rig.synth.turnPanelControl(Slot::A, kFilterCutoffCc, 99);
    rig.tick();
    rig.runCheckUntilItStops();

    requireFinished(rig);
    CHECK(rig.outcome(Item::CcsTransmitted) == Outcome::Passed);
}

TEST_CASE("Connection check: the synth does not receive CCs, so turn on C")
{
    EditorAndSynth rig;
    rig.synth.setReceivesControlChanges(false);
    const auto cutoffBefore = rig.synth.editBuffer(Slot::A)[kFilterCutoffOffset];

    rig.startCheck();

    requireFinished(rig);
    CHECK(rig.outcome(Item::CcsTransmitted) == Outcome::Passed);
    CHECK(rig.outcome(Item::CcsReceived) == Outcome::Failed);
    CHECK_THAT(rig.check().result(Item::CcsReceived).fix, ContainsSubstring("Shift + PROG/CTRL, C on"));
    CHECK(rig.synth.editBuffer(Slot::A)[kFilterCutoffOffset] == cutoffBefore);
    // Nothing changed, so nothing is put back.
    CHECK(rig.ccsSent().size() == 1);
}

TEST_CASE("Connection check: with C off on the synth, both CC tests fail")
{
    EditorAndSynth rig;
    rig.synth.setControlChangeOn(false);

    rig.startCheck();
    rig.waitOutUserStep();

    requireFinished(rig);
    CHECK(rig.outcome(Item::CcsTransmitted) == Outcome::Failed);
    CHECK(rig.outcome(Item::CcsReceived) == Outcome::Failed);
}

TEST_CASE("Connection check: the CC reception test uses the first Slot that plays a Program")
{
    EditorAndSynth rig;
    rig.synth.playPercussionKit(Slot::A, {});
    const auto cutoffBefore = rig.synth.editBuffer(Slot::B)[kFilterCutoffOffset];

    rig.startCheck();

    requireFinished(rig);
    CHECK(rig.outcome(Item::CcsReceived) == Outcome::Passed);
    CHECK_THAT(rig.check().result(Item::CcsReceived).explanation, ContainsSubstring("Slot B"));
    const auto ccs = rig.ccsSent();
    REQUIRE(ccs.size() == 2);
    CHECK(ccs[0][0] == (0xB0 | hardware::kDefaultSlotChannels[1]));
    CHECK(rig.synth.editBuffer(Slot::B)[kFilterCutoffOffset] == cutoffBefore);
}

TEST_CASE("Connection check: a wrong manual Global Channel names the one the synth uses")
{
    EditorAndSynth rig;
    rig.synth.setGlobalChannel(7);
    rig.engine.setManualGlobalChannel(GlobalChannel { 2 });

    rig.startCheck();

    requireFinished(rig);
    CHECK(rig.outcome(Item::SysExAnswered) == Outcome::Passed);
    CHECK(rig.outcome(Item::GlobalChannel) == Outcome::Failed);
    CHECK(rig.check().answeringGlobalChannel == GlobalChannel { 7 });
    const auto& result = rig.check().result(Item::GlobalChannel);
    // Counted as the synth shows it (HardwareAssumptions.h): dev 7 is Global Channel 8.
    CHECK_THAT(result.explanation, ContainsSubstring("set to Global Channel 3"));
    CHECK_THAT(result.explanation, ContainsSubstring("answers on Global Channel 8"));
    CHECK_THAT(result.fix, ContainsSubstring("SHIFT and press MIDI CH, then press MIDI CH again"));
    CHECK_THAT(result.fix, ContainsSubstring("G.8"));
    // The manual channel was asked first.
    CHECK(rig.toSynth.messages().front() == slotEditBufferRequest(2, Slot::A));
}

TEST_CASE("Manual Global Channel: connecting asks only that channel, never probes")
{
    EditorAndSynth rig;
    rig.synth.setGlobalChannel(5);

    SECTION("the synth answers on it")
    {
        rig.engine.setManualGlobalChannel(GlobalChannel { 5 });
        rig.connect(GlobalChannel { 9 });

        CHECK(rig.engine.connectionStatus() == ConnectionStatus::Connected);
        CHECK(rig.engine.globalChannel() == GlobalChannel { 5 });
        for (const auto& message : rig.toSynth.messages())
            CHECK(message[2] == 5);
    }

    SECTION("the synth does not answer on it")
    {
        rig.engine.setManualGlobalChannel(GlobalChannel { 2 });
        rig.connect();

        CHECK(rig.engine.connectionStatus() == ConnectionStatus::NoAnswer);
        CHECK_FALSE(rig.engine.globalChannel());
        const std::vector<Bytes> expected { slotEditBufferRequest(2, Slot::A) };
        CHECK(rig.toSynth.messages() == expected);
    }

    SECTION("back to Automatic, probing finds it again")
    {
        rig.engine.setManualGlobalChannel(GlobalChannel { 2 });
        rig.engine.setManualGlobalChannel(std::nullopt);
        rig.connect();

        CHECK(rig.engine.globalChannel() == GlobalChannel { 5 });
    }
}

TEST_CASE("Connecting cancels a running Connection check")
{
    EditorAndSynth rig;
    rig.synth.setAnswersSysEx(false);
    rig.engine.startConnectionCheck(rig.now);
    rig.tick();
    REQUIRE(rig.check().state == ConnectionCheckState::Running);

    rig.synth.setAnswersSysEx(true);
    rig.connect();

    CHECK(rig.check().state == ConnectionCheckState::Idle);
    CHECK(rig.engine.connectionStatus() == ConnectionStatus::Connected);
}

TEST_CASE("While requests are suspended (the Probe runs) the engine sends no request and takes no reply")
{
    EditorAndSynth rig;
    rig.synth.setGlobalChannel(hardware::kFactoryGlobalChannel);
    // The Probe's replies never reach the engine through the simulator here: the test plays
    // them in by hand.
    rig.synth.setAnswersSysEx(false);

    rig.engine.connect(std::nullopt, rig.now);
    REQUIRE(rig.toSynth.messages().size() == 1);
    rig.engine.setRequestsSuspended(true, rig.now);
    CHECK(rig.engine.requestsSuspended());

    // A reply to the Probe's own request arrives: the engine must not take it for its own.
    rig.pending.push_back(programEditBufferDump(hardware::kFactoryGlobalChannel, Slot::A,
                                                std::vector<std::int8_t>(kProgramParameterCount, 1)));
    for (int i = 0; i < 500; ++i)
        rig.tick();
    CHECK(rig.toSynth.messages().size() == 1);
    CHECK_FALSE(rig.engine.globalChannel());
    CHECK(rig.engine.connectionStatus() == ConnectionStatus::SearchingGlobalChannel);

    // A Connection check cannot start meanwhile.
    rig.engine.startConnectionCheck(rig.now);
    CHECK(rig.check().state == ConnectionCheckState::Idle);

    // Resumed: the interrupted request goes out again and the connection completes.
    rig.synth.setAnswersSysEx(true);
    rig.engine.setRequestsSuspended(false, rig.now);
    for (int i = 0; i < 1000 && rig.engine.connectionStatus() != ConnectionStatus::Connected; ++i)
        rig.tick();
    CHECK(rig.engine.connectionStatus() == ConnectionStatus::Connected);
    CHECK(rig.toSynth.messages()[1] == slotEditBufferRequest(hardware::kFactoryGlobalChannel, Slot::A));
    CHECK(rig.engine.globalChannel() == hardware::kFactoryGlobalChannel);
}

TEST_CASE("Suspending requests cancels a running Connection check")
{
    EditorAndSynth rig;
    rig.engine.startConnectionCheck(rig.now);
    REQUIRE(rig.check().state == ConnectionCheckState::Running);

    rig.engine.setRequestsSuspended(true, rig.now);
    CHECK(rig.check().state == ConnectionCheckState::Idle);
    const auto sent = rig.toSynth.messages().size();
    for (int i = 0; i < 500; ++i)
        rig.tick();
    CHECK(rig.toSynth.messages().size() == sent);
}
