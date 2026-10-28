#pragma once

#include "nl2x/Audition.h"
#include "nl2x/CcParameters.h"
#include "nl2x/ConnectionCheck.h"
#include "nl2x/MidiOutput.h"
#include "nl2x/Performance.h"
#include "nl2x/ProgramParameters.h"
#include "nl2x/Slot.h"
#include "nl2x/SlotChannels.h"
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

class PerformanceEditBuffer;
class ConnectionCheck;
class SynthConnection;

// Everything the editor does, behind one seam: MIDI messages in, MIDI messages out.
// Single-threaded: the caller invokes it from one thread only (CODING_STANDARDS.md).
class Engine {
public:
    explicit Engine(MidiOutput& output);
    ~Engine();

    // A complete MIDI message received from the synth. A CC of a CcParameter on a channel updates
    // the Edit Buffer of every Slot playing a Program that listens on that channel
    // (slotsReceivingOn); nothing is sent back. Other controllers are ignored.
    void handleIncomingMidi(std::span<const std::uint8_t> message, TimeMs timeMs);

    // Called regularly by the caller; drives throttling, timeouts and pacing.
    void advanceTime(TimeMs timeMs);

    // The user (GUI or DAW automation) set a parameter of a Slot's Program, in the parameter's
    // own range (CcParameterInfo::minValue … maxValue): its CC goes out on the Slot's channel and
    // the Slot's Edit Buffer, once read, holds the new value.
    void setParameter(Slot slot, CcParameter parameter, int value);

    // Connects to the synth (MIDI ports opened, app started, or the user asked): finds the Global
    // Channel by asking each possible one for Slot A's Edit Buffer, `rememberedGlobalChannel`
    // first, then reads the Edit Buffers of Slots B, C and D and the Performance Edit Buffer. With
    // a manual Global Channel only that one is asked. Anything still pending from an earlier
    // connection is dropped, the Performance read from it included, and a running Connection
    // check is cancelled.
    void connect(std::optional<GlobalChannel> rememberedGlobalChannel, TimeMs timeMs);

    // "Read from synth": reads one Slot's Edit Buffer again. Finds the Global Channel first when
    // it is not known yet. Ignored while the Global Channel is being searched for.
    void readSlot(Slot slot, TimeMs timeMs);

    // "Read from synth" for the Performance: reads the Performance Edit Buffer (and so the Slot
    // Channels) again. Connects first when the Global Channel is not known yet. Ignored while the
    // Global Channel is being searched for.
    void readPerformance(TimeMs timeMs);

    // The user (GUI or DAW automation) changed a Performance setting (`slot` is ignored for
    // settings that are not per Slot). Accepted edits go to the synth's Performance Edit Buffer as
    // one Performance dump per kPerformanceDumpIntervalMs at most, on a later advanceTime().
    PerformanceEditResult setPerformanceSetting(PerformanceSetting setting, Slot slot, int value);

    // Pins a Slot's channel regardless of the Performance (a service setting the shell keeps);
    // empty releases it.
    void setSlotChannelOverride(Slot slot, std::optional<MidiChannel> channel);
    std::optional<MidiChannel> slotChannelOverride(Slot slot) const { return channelOverrides[slotIndex(slot)]; }

    // Fixes the Global Channel (spec #1, user story 4): connecting then asks only this one and never
    // probes. Empty = Automatic, the default. Forgets a found Global Channel that differs; the
    // caller connects again afterwards.
    void setManualGlobalChannel(std::optional<GlobalChannel> channel);
    std::optional<GlobalChannel> manualGlobalChannel() const { return manualChannel; }

    // While suspended the engine sends no requests and takes no replies, because another tool
    // talks to the synth (the Probe window running a script). Requests queued meanwhile go out
    // after resuming; a running Connection check is cancelled. CCs from parameter changes still
    // go out.
    void setRequestsSuspended(bool suspended, TimeMs timeMs);
    bool requestsSuspended() const { return suspended; }

    // The Connection check (ConnectionCheck.h). Starting is ignored while requests are suspended.
    void startConnectionCheck(TimeMs timeMs);
    // The user will not do what the check's interactive step asks.
    void skipConnectionCheckStep(TimeMs timeMs);
    void cancelConnectionCheck();
    const ConnectionCheckReport& connectionCheck() const;

    // The Synth model, for the GUI.
    const SlotState& slotState(Slot slot) const { return slots[slotIndex(slot)]; }
    // The Global Channel the synth answered on; empty until one has. The shell remembers it.
    std::optional<GlobalChannel> globalChannel() const { return foundGlobalChannel; }
    ConnectionStatus connectionStatus() const { return status; }
    PerformanceStatus performanceStatus() const;
    // The Performance Edit Buffer as last read, with the edits made since; empty until read.
    const std::optional<Performance>& performance() const;
    // The channel the engine sends a Slot's CCs on (empty = off): the manual override if any, else
    // the Performance's Slot Channel once read, else hardware::kDefaultSlotChannels.
    std::optional<MidiChannel> slotChannel(Slot slot) const;
    // Slots that an edit of one Slot also reaches (shared channels, Layer), for the GUI.
    std::vector<SlotChannelWarning> slotChannelWarnings() const;

    // Audition (#11, EngineAudition.cpp): a Program or Percussion Kit into a Slot's Edit Buffer, a
    // Performance into the Performance Edit Buffer; never into a Bank. An accepted dump goes out
    // when no request is waiting for its reply; a later Audition for the same Edit Buffer replaces
    // one not sent yet. An auditioned Program becomes the Slot's Edit Buffer in the Synth model at
    // once; after a Performance has gone out, the Slots and the Performance are read again, since
    // the synth decides what it keeps (Kit references, settings it does not receive).
    AuditionResult auditionProgram(Slot slot, const Program& program, TimeMs timeMs);
    AuditionResult auditionPercussionKit(Slot slot, const PercussionKit& kit, TimeMs timeMs);
    AuditionResult auditionPerformance(const Performance& performance, TimeMs timeMs);

    // Changes whenever anything above changes: the shell polls it and redraws (or saves the
    // Global Channel) only when it moved.
    std::uint64_t stateRevision() const { return revision; }

private:
    // Asks `candidates[index]` for `slot`'s Edit Buffer; on an answer keeps that Global Channel
    // and, when `readOtherSlots`, goes on to read the other three Slots.
    void probe(std::vector<GlobalChannel> candidates, std::size_t index, Slot slot, bool readOtherSlots,
               TimeMs timeMs);
    void queueSlotRead(Slot slot, TimeMs timeMs);
    void queuePerformanceRead(TimeMs timeMs);
    void applySlotReply(Slot slot, std::span<const std::uint8_t> reply);
    void applyControlChange(MidiChannel channel, std::uint8_t cc, std::uint8_t data);
    void setStatus(ConnectionStatus newStatus);
    void changed() { ++revision; }

    // An auditioned dump not sent yet; `slot` is empty for the Performance Edit Buffer.
    struct PendingAudition {
        std::optional<Slot> slot;
        bool percussionKit = false;
        std::vector<std::int8_t> values;
    };
    void queueAudition(PendingAudition audition, TimeMs timeMs);
    void sendDueAudition(TimeMs timeMs);

    MidiOutput& output;
    std::unique_ptr<SynthConnection> connection;
    std::unique_ptr<PerformanceEditBuffer> performanceBuffer;
    std::array<std::optional<MidiChannel>, 4> channelOverrides;
    // Declared after `connection`, which holds callbacks into it, so that it goes first.
    std::unique_ptr<ConnectionCheck> check;
    std::array<SlotState, 4> slots;
    std::optional<GlobalChannel> foundGlobalChannel;
    std::optional<GlobalChannel> manualChannel;
    bool suspended = false;
    ConnectionStatus status = ConnectionStatus::NotConnected;
    // Slot reads queued on the known Global Channel and not yet finished.
    int pendingSlotReads = 0;
    bool slotReadFailed = false;
    std::uint64_t revision = 0;
    std::vector<PendingAudition> pendingAuditions;
};

} // namespace nl2x
