#pragma once

#include "nl2x/MidiOutput.h"
#include "nl2x/ProgramParameters.h"
#include "nl2x/Slot.h"
#include "nl2x/SynthState.h"
#include "nl2x/SysEx.h"
#include "nl2x/TimeMs.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace nl2x {

class SynthConnection;

// Everything the editor does, behind one seam: MIDI messages in, MIDI messages out.
// Single-threaded: the caller invokes it from one thread only (CODING_STANDARDS.md).
class Engine {
public:
    explicit Engine(MidiOutput& output);
    ~Engine();

    // A complete MIDI message received from the synth.
    void handleIncomingMidi(std::span<const std::uint8_t> message, TimeMs timeMs);

    // Called regularly by the caller; drives throttling, timeouts and pacing.
    void advanceTime(TimeMs timeMs);

    // The user (GUI or DAW automation) set a parameter of a Slot's Program, in the parameter's
    // own range (ProgramParameterInfo::minValue … maxValue).
    void setParameter(Slot slot, ProgramParameter parameter, int value);

    // Connects to the synth (MIDI ports opened, app started, or the user asked): finds the Global
    // Channel by asking each possible one for Slot A's Edit Buffer, `rememberedGlobalChannel`
    // first, then reads the Edit Buffers of Slots B, C and D. Anything still pending from an
    // earlier connection is dropped.
    void connect(std::optional<GlobalChannel> rememberedGlobalChannel, TimeMs timeMs);

    // "Read from synth": reads one Slot's Edit Buffer again. Finds the Global Channel first when
    // it is not known yet. Ignored while the Global Channel is being searched for.
    void readSlot(Slot slot, TimeMs timeMs);

    // The Synth model, for the GUI.
    const SlotState& slotState(Slot slot) const { return slots[slotIndex(slot)]; }
    // The Global Channel the synth answered on; empty until one has. The shell remembers it.
    std::optional<GlobalChannel> globalChannel() const { return foundGlobalChannel; }
    ConnectionStatus connectionStatus() const { return status; }

    // Changes whenever anything above changes: the shell polls it and redraws (or saves the
    // Global Channel) only when it moved.
    std::uint64_t stateRevision() const { return revision; }

private:
    // Asks `candidates[index]` for `slot`'s Edit Buffer; on an answer keeps that Global Channel
    // and, when `readOtherSlots`, goes on to read the other three Slots.
    void probe(std::vector<GlobalChannel> candidates, std::size_t index, Slot slot, bool readOtherSlots,
               TimeMs timeMs);
    void queueSlotRead(Slot slot, TimeMs timeMs);
    void applySlotReply(Slot slot, std::span<const std::uint8_t> reply);
    void setStatus(ConnectionStatus newStatus);
    void changed() { ++revision; }

    MidiOutput& output;
    std::unique_ptr<SynthConnection> connection;
    // Empty when a Slot's channel is off.
    std::array<std::optional<MidiChannel>, 4> slotChannels;
    std::array<SlotState, 4> slots;
    std::optional<GlobalChannel> foundGlobalChannel;
    ConnectionStatus status = ConnectionStatus::NotConnected;
    // Slot reads queued on the known Global Channel and not yet finished.
    int pendingSlotReads = 0;
    bool slotReadFailed = false;
    std::uint64_t revision = 0;
};

} // namespace nl2x
