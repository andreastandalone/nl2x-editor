// The Engine's undo and redo (#14), kept apart from Engine.cpp. Only edits the shell marks as the
// user's (beginUserEdit … endUserEdit) are recorded; undo and redo put values back through the
// same paths as an edit, so the synth follows exactly as it does for a knob.

#include "nl2x/Engine.h"

#include "EditHistory.h"
#include "PerformanceEditBuffer.h"

namespace nl2x {

EditStepId Engine::newEditStep()
{
    return ++lastEditStep;
}

void Engine::beginUserEdit(EditStepId step)
{
    userEditStep = step;
}

void Engine::endUserEdit()
{
    userEditStep.reset();
}

bool Engine::canUndo() const
{
    return history->canUndo();
}

bool Engine::canRedo() const
{
    return history->canRedo();
}

void Engine::undo(TimeMs timeMs)
{
    // Never while a user edit is being handed in: its values would be recorded as a new step.
    if (userEditStep)
        return;
    const auto changes = history->takeUndo();
    if (! changes)
        return;
    for (auto change = changes->rbegin(); change != changes->rend(); ++change)
        applyEdit(change->target, change->before, timeMs);
    changed();
}

void Engine::redo(TimeMs timeMs)
{
    if (userEditStep)
        return;
    const auto changes = history->takeRedo();
    if (! changes)
        return;
    for (const auto& change : *changes)
        applyEdit(change.target, change.after, timeMs);
    changed();
}

void Engine::recordUserEdit(const EditTarget& target, int before, int after)
{
    if (userEditStep)
        history->record(*userEditStep, target, before, after);
}

void Engine::applyEdit(const EditTarget& target, int value, TimeMs timeMs)
{
    switch (target.kind) {
    case EditKind::CcParameter:
        setParameter(target.slot, static_cast<CcParameter>(target.id), value);
        break;
    case EditKind::MorphAmount:
        // Through the throttled Program dumps, as any Morph Amount edit.
        setMorphAmount(target.slot, static_cast<ProgramParameter>(target.id), value, timeMs);
        break;
    case EditKind::PerformanceSetting: {
        const auto before = performanceBuffer->performance();
        performanceBuffer->restore(static_cast<PerformanceSetting>(target.id), target.slot, value);
        if (performanceBuffer->performance() != before)
            changed();
        break;
    }
    }
}

void Engine::forgetEdits(const std::array<bool, 4>& slotsReplaced, bool performanceReplaced)
{
    if (history->forget([&](const EditTarget& target) {
            return target.inSlotEditBuffer() ? slotsReplaced[slotIndex(target.slot)] : performanceReplaced;
        }))
        changed();
}

} // namespace nl2x
