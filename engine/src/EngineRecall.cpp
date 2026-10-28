// The Engine's project recall (#8), kept apart from Engine.cpp. Project recall only ever sends
// Edit Buffer dumps (type 30 for the Performance, type 0 for Programs): never a Bank dump, and
// never a CC.

#include "nl2x/Engine.h"

#include "ConnectionCheck.h"
#include "PerformanceEditBuffer.h"
#include "ProgramDumpThrottle.h"
#include "ProjectRecall.h"
#include "SelectionTracker.h"
#include "SynthConnection.h"

#include "nl2x/HardwareAssumptions.h"
#include "nl2x/MemoryWriter.h"

namespace nl2x {

std::vector<std::uint8_t> Engine::projectState() const
{
    return encodeProjectState({ slots, performanceBuffer->performance() });
}

ProjectLoadResult Engine::loadProjectState(std::span<const std::uint8_t> state, bool sendToSynth, TimeMs timeMs)
{
    const auto sounds = decodeProjectState(state);
    if (! sounds)
        return ProjectLoadResult::Rejected;
    if (! recall)
        recall = std::make_unique<ProjectRecall>();

    // While the Global Channel is being searched for, the connection goes on: what follows it
    // (recall or reads) is decided when it is found.
    const bool searching = status == ConnectionStatus::SearchingGlobalChannel;
    const bool connected = foundGlobalChannel.has_value() && ! searching;
    // The memory writer's own requests must survive: while it is busy nothing is dropped, and the
    // recall waits until it is idle (startProjectRecall).
    if (connected && ! memory->busy())
        // Reads still pending would overwrite the project's sounds, or be overwritten by them.
        dropPendingReads();
    *recall = {};
    pendingAuditions.clear();
    programDumps->dropAllUnsentEdits();

    slots = sounds->slots;
    performanceBuffer->restore(sounds->performance);
    // Every Edit Buffer was replaced from outside: undoing an edit made before would land on
    // another sound. Project load itself is no user edit and is never recorded (#14).
    forgetEdits({ true, true, true, true }, true);
    changed();

    if (! sendToSynth) {
        // The synth's sounds replace the project's (as #5 does for the Performance parameters).
        if (connected) {
            setStatus(ConnectionStatus::ReadingSlots);
            for (auto slot : kAllSlots)
                queueSlotRead(slot, timeMs);
            queuePerformanceRead(timeMs);
        }
        return ProjectLoadResult::NotSent;
    }
    if (! connected) {
        recall->held = true;
        return ProjectLoadResult::HeldUntilConnected;
    }
    startProjectRecall(timeMs);
    return recall->waitingForMemoryWriter ? ProjectLoadResult::HeldUntilMemoryWriterIdle : ProjectLoadResult::Sending;
}

bool Engine::projectRecallHeld() const
{
    return recall && recall->held;
}

void Engine::holdUnfinishedProjectRecall()
{
    if (recall && (! recall->dumps.empty() || recall->waitingForMemoryWriter)) {
        // Rebuilt from the Synth model on the next connection, on its Global Channel.
        *recall = {};
        recall->held = true;
    }
}

void Engine::dropPendingReads()
{
    check->cancel();
    connection->clear();
    pendingSlotReads = 0;
    slotReadFailed = false;
    selectionTracker->due.reset();
    selectionTracker->rereadSlots = {};
    selectionTracker->rereadPerformance = false;
    if (performanceBuffer->status() == PerformanceStatus::Reading)
        performanceBuffer->readFailed();
}

void Engine::startProjectRecall(TimeMs timeMs)
{
    *recall = {};
    // Nothing may reach the synth's Edit Buffers while the memory writer stores or reads (#12):
    // the recall starts once it is idle (sendDueProjectRecall).
    if (memory->busy()) {
        recall->waitingForMemoryWriter = true;
        setStatus(ConnectionStatus::Connected);
        return;
    }
    // The dumps carry every edit: none must follow them as a separate dump.
    performanceBuffer->dropUnsentEdits();
    programDumps->dropAllUnsentEdits();

    if (const auto& performance = performanceBuffer->performance()) {
        // Built as a Performance edit is (PerformanceEditBuffer::takeDueDump): each Slot playing a
        // Program carries its Edit Buffer; a Kit Slot keeps the bytes read (its Kit reference).
        auto sent = *performance;
        for (auto slot : kAllSlots) {
            const auto& state = slots[slotIndex(slot)];
            if (state.content == SlotContent::Program && state.program)
                sent.setProgram(slot, *state.program);
        }
        recall->dumps.push_back(performanceEditBufferDump(*foundGlobalChannel, sent.bytes()));
    } else {
        recall->readPerformanceAfter = true;
    }

    for (auto slot : kAllSlots) {
        const auto& state = slots[slotIndex(slot)];
        const bool playsProgram = state.content == SlotContent::Program && state.program;
        if (playsProgram
            && (hardware::kProjectRecallSendsSlotDumpsAfterPerformance || ! performanceBuffer->performance()))
            recall->dumps.push_back(programEditBufferDump(*foundGlobalChannel, slot, state.program->bytes()));
        // A Kit (kept as a reference only) or an unknown Slot holds nothing of the project to lose.
        recall->readSlotsAfter[slotIndex(slot)] = ! playsProgram;
    }

    setStatus(ConnectionStatus::Connected);
    if (recall->dumps.empty())
        readWhatProjectRecallLacks(timeMs);
    else
        sendDueProjectRecall(timeMs);
}

void Engine::sendDueProjectRecall(TimeMs timeMs)
{
    // Dumps wait while another tool talks to the synth, and until no reply is expected; each one
    // keeps the usual gap after it, so the next goes out on a later tick.
    if (recall && recall->waitingForMemoryWriter && foundGlobalChannel
        && status != ConnectionStatus::SearchingGlobalChannel && ! memory->busy()) {
        // Reads queued meanwhile would overwrite the project's sounds.
        dropPendingReads();
        startProjectRecall(timeMs);
        return;
    }
    if (! recall || recall->dumps.empty() || ! foundGlobalChannel || suspended || memory->holdsEditBufferDumps()
        || ! connection->isIdle(timeMs))
        return;

    connection->sendWithoutReply(recall->dumps.front(), timeMs);
    recall->dumps.pop_front();
    if (recall->dumps.empty())
        readWhatProjectRecallLacks(timeMs);
}

void Engine::readWhatProjectRecallLacks(TimeMs timeMs)
{
    // Everything the project holds is on the synth; read what it does not hold.
    for (auto slot : kAllSlots) {
        if (! recall->readSlotsAfter[slotIndex(slot)])
            continue;
        if (pendingSlotReads == 0)
            slotReadFailed = false;
        setStatus(ConnectionStatus::ReadingSlots);
        queueSlotRead(slot, timeMs);
    }
    if (recall->readPerformanceAfter)
        queuePerformanceRead(timeMs);
    *recall = {};
}

} // namespace nl2x
