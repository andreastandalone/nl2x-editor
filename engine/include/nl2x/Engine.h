#pragma once

#include "nl2x/Audition.h"
#include "nl2x/CcParameters.h"
#include "nl2x/ConnectionCheck.h"
#include "nl2x/HardwareAssumptions.h"
#include "nl2x/MemoryWriter.h"
#include "nl2x/EditStep.h"
#include "nl2x/MidiOutput.h"
#include "nl2x/MorphAmounts.h"
#include "nl2x/Performance.h"
#include "nl2x/ProgramParameters.h"
#include "nl2x/Selection.h"
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

class EditHistory;
class PerformanceEditBuffer;
struct EditTarget;
class ConnectionCheck;
class ProgramDumpThrottle;
class SynthConnection;
struct SelectionTracker;

// Everything the editor does, behind one seam: MIDI messages in, MIDI messages out.
// Single-threaded: the caller invokes it from one thread only (CODING_STANDARDS.md).
class Engine {
public:
    explicit Engine(MidiOutput& output);
    // Tests only: an engine following other readings of the unverified hardware switches.
    Engine(MidiOutput& output, hardware::Switches switches);
    ~Engine();

    // A complete MIDI message received from the synth. A CC of a CcParameter on a channel updates
    // the Edit Buffer of every Slot playing a Program that listens on that channel
    // (slotsReceivingOn); nothing is sent back. Other controllers are ignored.
    void handleIncomingMidi(std::span<const std::uint8_t> message, TimeMs timeMs);

    // Called regularly by the caller; drives throttling, timeouts and pacing.
    void advanceTime(TimeMs timeMs);

    // The user (GUI or DAW automation) set a parameter of a Slot's Program, in the parameter's
    // own range (CcParameterInfo::minValue … maxValue): its CC goes out on the Slot's channel and
    // the Slot's Edit Buffer, once read, holds the new value. When CC35 does not reach the
    // ring-mod bit (hardware::kRingModReachableByCc35), a change of that bit goes out with the
    // Slot's next throttled Program dump instead, as a Morph Amount does.
    void setParameter(Slot slot, CcParameter parameter, int value);

    // Morph Amounts (#7, EngineMorph.cpp). The user (GUI or DAW automation) set the Morph Amount of
    // a morphable base parameter of a Slot playing a Program, −128…127 (0 = off; values outside are
    // brought into it). It goes into the Slot's Edit Buffer and reaches the synth as a whole
    // Program dump to that Edit Buffer (type 0, never a Bank dump), carrying every other edit too:
    // at once when the Slot's last dump is kProgramDumpIntervalMs old and nothing waits for a
    // reply, otherwise coalesced and sent on a later advanceTime(). Nothing goes out while
    // requests are suspended. Edits not sent yet survive a read of the Slot; a selection, an
    // Audition or a new connection drops them.
    MorphEditResult setMorphAmount(Slot slot, ProgramParameter baseParameter, int amount, TimeMs timeMs);

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

    // Selection (#10, EngineSelection.cpp). Selects a stored Program or Percussion Kit for a Slot:
    // Bank Select (CC32) and Program Change on the Slot's channel. Every Slot receiving on that
    // channel switches; once the synth has settled (kSelectionSettleMs) their Edit Buffers and the
    // Performance are read again on a later advanceTime().
    SelectionResult selectProgram(Slot slot, ProgramSelection selection, TimeMs timeMs);
    // Selects a stored Performance: Bank Select and Program Change on the Global Channel; then the
    // Performance (and so the Slot Channels) and all four Slots are read again.
    SelectionResult selectPerformance(PerformanceSelection selection, TimeMs timeMs);
    // What is selected: set by the two calls above, by Program Changes the synth sends (the user
    // turned the dial on the synth), and, for Slots with nothing selected, by the Performance read
    // (its pgmselect and bankselect bytes). A Performance change empties the Slots' selections
    // until the Performance is read again.
    const SelectionState& selection() const;
    // Project load: shows `state` as selected and sends nothing (sending on load is #8's job).
    void restoreSelection(const SelectionState& state);

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

    // Writing to the synth's memory and reading Banks (#12, MemoryWriter.h). Connecting again
    // aborts whatever it is doing.
    MemoryWriter& memoryWriter() { return *memory; }
    const MemoryWriter& memoryWriter() const { return *memory; }

    // Undo/redo (#14, EngineEditHistory.cpp). Only the user's edits in the plugin are undoable: the
    // shell hands each one in between beginUserEdit(step) and endUserEdit(), with the same step
    // for every value of one knob drag and a new step (newEditStep) for each menu choice. Calls of
    // setParameter, setMorphAmount and setPerformanceSetting outside that (DAW automation), CCs
    // from the synth, reads, Audition, Selection and project load are not recorded. When the
    // plugin's copy of an Edit Buffer is replaced (connecting, a read that finds other values, an
    // Audition, a Selection), the edits of that Edit Buffer leave the history: undoing them would
    // land on a different sound. At most EditHistory::kMaxSteps (200) steps are kept; a new step
    // empties redo.
    EditStepId newEditStep();
    void beginUserEdit(EditStepId step);
    void endUserEdit();
    bool canUndo() const;
    bool canRedo() const;
    // Puts back the values from before the latest step (redo: after it) and sends them as an edit
    // would: CCs on the Slot Channel, Performance settings in the next coalesced Performance dump,
    // Morph Amounts in the Slot's next throttled Program dump. The shell moves the DAW parameters
    // by following the engine. Ignored inside a user edit.
    void undo(TimeMs timeMs);
    void redo(TimeMs timeMs);

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
    // Selection (EngineSelection.cpp).
    void followSelectionOnSynth(std::span<const std::uint8_t> message, TimeMs timeMs);
    void rereadAfterSelection(TimeMs timeMs);
    void takeSelectionFromPerformance();
    std::optional<GlobalChannel> selectionGlobalChannel() const;
    // The Slots a Program Change on `channel` reaches (shared channel, Layer).
    std::array<bool, 4> slotsReachedByProgramChange(MidiChannel channel) const;
    void sendSelection(MidiChannel channel, BankAndProgramChange midi);

    void changed() { ++revision; }

    // Program dumps for edits without a CC (EngineMorph.cpp).
    void sendDueProgramDumps(TimeMs timeMs);
    void dropUnsentProgramEdits(const std::array<bool, 4>& slotsReplaced);
    void setRingModSyncWithoutRingModCc(Slot slot, int value);
    void applySyncOnlyControlChange(MidiChannel channel, std::uint8_t data);
    // Undo/redo (EngineEditHistory.cpp). Every user-editable setter calls recordUserEdit with the
    // value before and after it changed the model; applyEdit puts a value back through that setter.
    void recordUserEdit(const EditTarget& target, int before, int after);
    void applyEdit(const EditTarget& target, int value, TimeMs timeMs);
    // The Edit Buffers whose copy was replaced from outside: their edits leave the history.
    void forgetEdits(const std::array<bool, 4>& slotsReplaced, bool performanceReplaced);

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
    std::unique_ptr<SelectionTracker> selectionTracker;
    std::unique_ptr<ProgramDumpThrottle> programDumps;
    hardware::Switches switches;
    std::array<std::optional<MidiChannel>, 4> channelOverrides;
    // Declared after `connection`, which holds callbacks into it, so that it goes first.
    std::unique_ptr<ConnectionCheck> check;
    std::unique_ptr<MemoryWriter> memory;
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
    std::unique_ptr<EditHistory> history;
    // The step of the user edit being handed in; empty outside beginUserEdit … endUserEdit.
    std::optional<EditStepId> userEditStep;
    EditStepId lastEditStep = 0;
};

} // namespace nl2x
