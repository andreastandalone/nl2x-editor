// The Engine's Audition (#11), kept apart from Engine.cpp. Audition only ever sends Edit Buffer
// dumps (type 0 for Programs and Kits, type 30 for Performances): never a Bank dump.

#include "nl2x/Engine.h"

#include "PerformanceEditBuffer.h"
#include "SynthConnection.h"

#include "nl2x/HardwareAssumptions.h"

#include <algorithm>

namespace nl2x {

namespace {

std::array<bool, 4> oneSlot(Slot slot)
{
    std::array<bool, 4> slots {};
    slots[slotIndex(slot)] = true;
    return slots;
}

std::string slotName(Slot slot)
{
    return std::string("Slot ") + slotLetter(slot);
}

} // namespace

std::string auditionRefusalText(AuditionResult result, Slot slot)
{
    switch (result) {
    case AuditionResult::Sent:
        return {};
    case AuditionResult::RefusedNotConnected:
        return "Not connected to the synth: nothing was sent.";
    case AuditionResult::RefusedSlotNotRead:
        return slotName(slot) + " has not been read from the synth yet: read it first, so that it is known whether "
                                "it plays a Program or a Percussion Kit.";
    case AuditionResult::RefusedSlotPlaysPercussionKit:
        return slotName(slot) + " is playing a Percussion Kit: a Program can only be auditioned on a Slot playing a "
                                "Program.";
    case AuditionResult::RefusedSlotPlaysProgram:
        return slotName(slot) + " is playing a Program: a Percussion Kit can only be auditioned on a Slot playing a "
                                "Percussion Kit.";
    }
    return {};
}

AuditionResult Engine::auditionProgram(Slot slot, const Program& program, TimeMs timeMs)
{
    if (! foundGlobalChannel)
        return AuditionResult::RefusedNotConnected;
    auto& state = slots[slotIndex(slot)];
    // A Program is received only into a Slot playing a Program [OFF-2X p.30].
    if (state.content == SlotContent::Unknown)
        return AuditionResult::RefusedSlotNotRead;
    if (state.content == SlotContent::PercussionKit)
        return AuditionResult::RefusedSlotPlaysPercussionKit;

    // The auditioned Program replaces the Edit Buffer, Morph edits not sent yet included.
    dropUnsentProgramEdits(oneSlot(slot));
    if (state.program != program) {
        state.program = program;
        changed();
    }
    const auto& bytes = program.bytes();
    queueAudition({ slot, false, { bytes.begin(), bytes.end() } }, timeMs);
    return AuditionResult::Sent;
}

AuditionResult Engine::auditionPercussionKit(Slot slot, const PercussionKit& kit, TimeMs timeMs)
{
    if (! foundGlobalChannel)
        return AuditionResult::RefusedNotConnected;
    const auto& state = slots[slotIndex(slot)];
    // A Kit is received only into a Slot playing a Kit [OFF-2X p.30].
    if (state.content == SlotContent::Unknown)
        return AuditionResult::RefusedSlotNotRead;
    if (state.content == SlotContent::Program)
        return AuditionResult::RefusedSlotPlaysProgram;

    PendingAudition audition { slot, true, {} };
    audition.values.reserve(kPercussionKitParameterCount);
    for (const auto& program : kit)
        audition.values.insert(audition.values.end(), program.bytes().begin(), program.bytes().end());
    queueAudition(std::move(audition), timeMs);
    return AuditionResult::Sent;
}

AuditionResult Engine::auditionPerformance(const Performance& performance, TimeMs timeMs)
{
    if (! foundGlobalChannel)
        return AuditionResult::RefusedNotConnected;
    // The whole Performance replaces the one being edited.
    performanceBuffer->dropUnsentEdits();
    // It also replaces every Slot's Program: a Program Audition still waiting would be undone by it.
    std::erase_if(pendingAuditions, [](const auto& pending) { return pending.slot && ! pending.percussionKit; });
    dropUnsentProgramEdits({ true, true, true, true });
    const auto& bytes = performance.bytes();
    queueAudition({ std::nullopt, false, { bytes.begin(), bytes.end() } }, timeMs);
    return AuditionResult::Sent;
}

void Engine::queueAudition(PendingAudition audition, TimeMs timeMs)
{
    std::erase_if(pendingAuditions, [&audition](const auto& pending) {
        return pending.slot == audition.slot && pending.percussionKit == audition.percussionKit;
    });
    pendingAuditions.push_back(std::move(audition));
    sendDueAudition(timeMs);
}

void Engine::sendDueAudition(TimeMs timeMs)
{
    // Dumps wait while another tool talks to the synth, and until no reply is expected.
    if (pendingAuditions.empty() || ! foundGlobalChannel || suspended || memory->holdsEditBufferDumps()
        || ! connection->isIdle(timeMs))
        return;

    const auto audition = std::move(pendingAuditions.front());
    pendingAuditions.erase(pendingAuditions.begin());

    if (! audition.slot) {
        connection->sendWithoutReply(performanceEditBufferDump(*foundGlobalChannel, audition.values), timeMs);
        // What the synth now plays: Kit references and settings it does not receive stay its own.
        for (auto slot : kAllSlots)
            readSlot(slot, timeMs);
        readPerformance(timeMs);
        return;
    }

    const auto spec = static_cast<std::uint8_t>(
        slotIndex(*audition.slot) + (audition.percussionKit ? hardware::kPercussionKitEditBufferSpecOfSlotA : 0));
    connection->sendWithoutReply(
        buildSysEx(*foundGlobalChannel, hardware::kProgramEditBufferDumpType, spec, audition.values), timeMs);
}

} // namespace nl2x
