#pragma once

#include "SynthConnection.h"

#include "nl2x/ConnectionCheck.h"
#include "nl2x/MidiOutput.h"
#include "nl2x/Program.h"
#include "nl2x/Slot.h"
#include "nl2x/SysEx.h"
#include "nl2x/TimeMs.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace nl2x {

// Runs the Connection check as a small state machine over the engine's seam: requests through
// the engine's SynthConnection (so they never overlap the engine's own), CCs straight to the
// MIDI output, every incoming message observed, time from advanceTime(). Steps in order:
//
// 1. Ask each Global Channel for Slot A's Edit Buffer (the manual or known one first).
// 2. If none answers: did any MIDI arrive at all? If not yet, ask the user to play a key; MIDI
//    arriving means the synth ignores SysEx, nothing arriving means the return cable is missing.
// 3. All Controllers Request: CCs in reply mean CCs are transmitted; none, ask the user to turn
//    a knob; still none means C is off.
// 4. Send Filter Cutoff to the first Slot playing a Program, read the Slot back, restore it:
//    no change means CCs are not received.
class ConnectionCheck {
public:
    struct Setup {
        // Set when the user fixed the Global Channel in the settings.
        std::optional<GlobalChannel> manualGlobalChannel;
        // The one the engine last found, asked first in Automatic.
        std::optional<GlobalChannel> knownGlobalChannel;
        // Where to send the test CC; empty when a Slot's channel is off.
        std::array<std::optional<MidiChannel>, 4> slotChannels {};
    };

    ConnectionCheck(SynthConnection& connection, MidiOutput& output, std::function<void()> changed);

    // Starts over, dropping anything from an earlier run.
    void start(const Setup& setup, TimeMs timeMs);
    // Withdraws the check's requests; a running check goes back to Idle, a finished one keeps
    // its results.
    void cancel();
    // The user will not (or cannot) do what the interactive step asks: that test is not tested.
    void skipUserStep(TimeMs timeMs);

    void handleIncomingMidi(std::span<const std::uint8_t> message, TimeMs timeMs);
    void advanceTime(TimeMs timeMs);

    const ConnectionCheckReport& report() const { return currentReport; }
    bool isActive() const;

private:
    enum class Step : std::uint8_t {
        None,
        FindingSynth,
        WaitingForAnyMidi,
        AllControllers,
        WaitingForKnob,
        ReadingSlot,
        SettlingCc,
        ReadingBack,
    };

    // What a Slot Edit Buffer request returned: a Program, or empty for a Percussion Kit.
    using SlotReading = std::optional<Program>;

    void askNextCandidate(TimeMs timeMs);
    void synthFound(GlobalChannel channel, std::span<const std::uint8_t> reply, TimeMs timeMs);
    void noSysExAnswer(TimeMs timeMs);
    void midiButNoSysEx();
    void noMidiAtAll();
    void startAllControllers(TimeMs timeMs);
    void ccsTransmitted(const std::string& explanation, TimeMs timeMs);
    void ccsNotTransmitted(TimeMs timeMs);
    void startCcReceive(TimeMs timeMs);
    void testNextSlot(TimeMs timeMs);
    void testSlot(Slot slot, const SlotReading& reading, TimeMs timeMs);
    void readBack(TimeMs timeMs);
    void waitForUser(Step step, std::string prompt, TimeMs timeMs);
    void finish();

    void set(ConnectionCheckItem item, ConnectionCheckOutcome outcome, std::string explanation = {},
             std::string fix = {});
    void setRemaining(ConnectionCheckOutcome outcome, const std::string& explanation);
    void request(std::vector<std::uint8_t> message, SynthConnection::Accepts accepts,
                 std::function<void(std::optional<std::span<const std::uint8_t>>, TimeMs)> done, TimeMs timeMs);

    SynthConnection& connection;
    MidiOutput& output;
    std::function<void()> changed;

    ConnectionCheckReport currentReport;
    Setup setup;
    Step step = Step::None;
    // Incremented by every start and cancel: a reply for an earlier run is ignored.
    int run = 0;

    std::vector<GlobalChannel> candidates;
    std::size_t candidateIndex = 0;
    bool midiSeen = false;
    bool ccSeen = false;

    // The CC reception test.
    std::array<std::optional<SlotReading>, 4> readings {};
    std::size_t slotUnderTest = 0;
    int originalCutoff = 0;
    TimeMs settleUntil = 0;
};

} // namespace nl2x
