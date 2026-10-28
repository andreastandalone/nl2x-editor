// The Engine's Selection (#10): selecting stored Programs, Percussion Kits and Performances by Bank
// Select and Program Change, following those the synth sends, and reading the Edit Buffers again
// afterwards. Kept apart from Engine.cpp.

#include "nl2x/Engine.h"

#include "PerformanceEditBuffer.h"
#include "SelectionTracker.h"
#include "SynthConnection.h"

#include "nl2x/HardwareAssumptions.h"

#include <algorithm>

namespace nl2x {

namespace {

constexpr std::uint8_t kControlChange = 0xB0;
constexpr std::uint8_t kProgramChange = 0xC0;

constexpr std::array<bool, 4> kNoSlots {};
constexpr std::array<bool, 4> kEverySlot { true, true, true, true };

} // namespace

void SelectionTracker::scheduleReread(TimeMs timeMs, const std::array<bool, 4>& slots, bool performance)
{
    due = timeMs + hardware::kSelectionSettleMs;
    for (std::size_t i = 0; i < slots.size(); ++i)
        rereadSlots[i] = rereadSlots[i] || slots[i];
    rereadPerformance = rereadPerformance || performance;
}

SelectionResult Engine::selectProgram(Slot slot, ProgramSelection selection, TimeMs timeMs)
{
    const auto midi = midiSelection(selection);
    if (! midi)
        return SelectionResult::RefusedInvalid;
    const auto channel = slotChannel(slot);
    if (! channel)
        return SelectionResult::RefusedSlotChannelOff;
    if (channel == selectionGlobalChannel())
        return SelectionResult::RefusedSlotOnGlobalChannel;

    sendSelection(*channel, *midi);
    const auto reached = slotsReachedByProgramChange(*channel);
    for (auto other : kAllSlots)
        if (reached[slotIndex(other)])
            selectionTracker->state.programs[slotIndex(other)] = selection;
    // Morph edits of the old Programs not sent yet must not land on the new ones.
    dropUnsentProgramEdits(reached);
    forgetEdits(reached, false);
    // The Performance too: its copy of the Program and its pgmselect/bankselect bytes change, and
    // a later Performance edit is sent as a whole dump built from that copy.
    selectionTracker->scheduleReread(timeMs, reached, true);
    changed();
    return SelectionResult::Sent;
}

SelectionResult Engine::selectPerformance(PerformanceSelection selection, TimeMs timeMs)
{
    const auto midi = midiSelection(selection);
    if (! midi)
        return SelectionResult::RefusedInvalid;
    const auto channel = selectionGlobalChannel();
    if (! channel)
        return SelectionResult::RefusedNoGlobalChannel;

    sendSelection(*channel, *midi);
    // Edits of the old Performance not sent yet must not land on the new one.
    performanceBuffer->dropUnsentEdits();
    dropUnsentProgramEdits(kEverySlot);
    forgetEdits(kEverySlot, true);
    selectionTracker->state.performance = selection;
    // The Slots now play the new Performance's Programs; the Performance read fills them in.
    selectionTracker->state.programs = {};
    selectionTracker->scheduleReread(timeMs, kEverySlot, true);
    changed();
    return SelectionResult::Sent;
}

const SelectionState& Engine::selection() const
{
    return selectionTracker->state;
}

void Engine::restoreSelection(const SelectionState& state)
{
    SelectionState valid;
    for (std::size_t i = 0; i < valid.programs.size(); ++i)
        if (state.programs[i] && isValid(*state.programs[i]))
            valid.programs[i] = state.programs[i];
    if (state.performance && isValid(*state.performance))
        valid.performance = state.performance;
    if (selectionTracker->state != valid) {
        selectionTracker->state = valid;
        changed();
    }
}

void Engine::sendSelection(MidiChannel channel, BankAndProgramChange midi)
{
    // Bank Select first: it takes effect with the next Program Change.
    const std::array<std::uint8_t, 3> bank { static_cast<std::uint8_t>(kControlChange | channel),
                                             hardware::kBankSelectController, midi.bankSelect };
    const std::array<std::uint8_t, 2> program { static_cast<std::uint8_t>(kProgramChange | channel),
                                                midi.programChange };
    output.send(bank);
    output.send(program);
    selectionTracker->bankSelect[channel] = midi.bankSelect;
}

void Engine::followSelectionOnSynth(std::span<const std::uint8_t> message, TimeMs timeMs)
{
    if (message.empty())
        return;
    const auto kind = static_cast<std::uint8_t>(message[0] & 0xF0);
    const auto channel = static_cast<MidiChannel>(message[0] & 0x0F);
    auto& tracker = *selectionTracker;

    if (kind == kControlChange && message.size() == 3 && message[1] == hardware::kBankSelectController) {
        tracker.bankSelect[channel] = message[2];
        return;
    }
    if (kind != kProgramChange || message.size() != 2)
        return;
    const int programChange = message[1];

    // The Global Channel wins over a Slot on the same channel [OFF-2X p.76].
    if (channel == selectionGlobalChannel()) {
        std::optional<int> bank;
        if (tracker.bankSelect[channel])
            bank = *tracker.bankSelect[channel];
        else if (tracker.state.performance)
            bank = midiSelection(*tracker.state.performance)->bankSelect;
        tracker.state.performance = bank ? performanceSelectionFromMidi(*bank, programChange) : std::nullopt;
        performanceBuffer->dropUnsentEdits();
        dropUnsentProgramEdits(kEverySlot);
        forgetEdits(kEverySlot, true);
        tracker.state.programs = {};
        tracker.scheduleReread(timeMs, kEverySlot, true);
        changed();
        return;
    }

    const auto reached = slotsReachedByProgramChange(channel);
    if (reached == kNoSlots)
        return;
    forgetEdits(reached, false);
    for (auto slot : kAllSlots) {
        if (! reached[slotIndex(slot)])
            continue;
        auto& selected = tracker.state.programs[slotIndex(slot)];
        std::optional<int> bank;
        if (tracker.bankSelect[channel])
            bank = *tracker.bankSelect[channel];
        else if (selected)
            bank = selected->bank + hardware::kBankSelectOfProgramBank0;
        selected = bank ? programSelectionFromMidi(*bank, programChange) : std::nullopt;
    }
    dropUnsentProgramEdits(reached);
    tracker.scheduleReread(timeMs, reached, true);
    changed();
}

void Engine::rereadAfterSelection(TimeMs timeMs)
{
    auto& tracker = *selectionTracker;
    if (! tracker.due || timeMs < *tracker.due)
        return;
    const auto slotsToRead = tracker.rereadSlots;
    const bool readPerformanceToo = tracker.rereadPerformance;
    tracker.due.reset();
    tracker.rereadSlots = {};
    tracker.rereadPerformance = false;

    // Without a known Global Channel nothing can be read; connecting reads everything anyway.
    if (! foundGlobalChannel || status == ConnectionStatus::SearchingGlobalChannel)
        return;
    // The Performance first: it tells the new Slot Channels.
    if (readPerformanceToo)
        queuePerformanceRead(timeMs);
    for (auto slot : kAllSlots) {
        if (! slotsToRead[slotIndex(slot)])
            continue;
        if (pendingSlotReads == 0)
            slotReadFailed = false;
        setStatus(ConnectionStatus::ReadingSlots);
        queueSlotRead(slot, timeMs);
    }
}

void Engine::takeSelectionFromPerformance()
{
    const auto& performance = performanceBuffer->performance();
    if (! performance)
        return;
    auto& programs = selectionTracker->state.programs;
    bool any = false;
    for (auto slot : kAllSlots) {
        auto& selected = programs[slotIndex(slot)];
        // An explicit or restored selection is kept: the Performance's bytes name where the Slot
        // was loaded from when the Performance was stored, which may be older.
        if (selected)
            continue;
        selected = programSelectionFromMidi(
            performance->setting(PerformanceSetting::ProgramBank, slot) + hardware::kBankSelectOfProgramBank0,
            performance->setting(PerformanceSetting::ProgramNumber, slot));
        any = any || selected.has_value();
    }
    if (any)
        changed();
}

std::optional<GlobalChannel> Engine::selectionGlobalChannel() const
{
    return foundGlobalChannel ? foundGlobalChannel : manualChannel;
}

std::array<bool, 4> Engine::slotsReachedByProgramChange(MidiChannel channel) const
{
    SlotChannelArray channels;
    for (auto slot : kAllSlots)
        channels[slotIndex(slot)] = slotChannel(slot);
    // Without the assumption a Program Change reaches only the Slots on exactly that channel.
    const auto layer = hardware::kProgramChangeReachesEverySlotReceivingOnItsChannel ? performanceBuffer->layer()
                                                                                     : SlotLayerArray {};
    std::array<bool, 4> reached {};
    for (auto slot : nl2x::slotsReceivingOn(channel, channels, layer))
        reached[slotIndex(slot)] = true;
    return reached;
}

} // namespace nl2x
