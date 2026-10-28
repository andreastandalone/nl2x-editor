#include "EditHistory.h"

#include <algorithm>

namespace nl2x {

EditTarget ccParameterTarget(Slot slot, CcParameter parameter)
{
    return { EditKind::CcParameter, slot, static_cast<std::uint8_t>(parameter) };
}

EditTarget performanceSettingTarget(PerformanceSetting setting, Slot slot)
{
    // One target per value: a setting that is not per Slot is the same whatever Slot came with it.
    const auto owner = performanceSettingInfo(setting).perSlot ? slot : Slot::A;
    return { EditKind::PerformanceSetting, owner, static_cast<std::uint8_t>(setting) };
}

EditTarget morphAmountTarget(Slot slot, ProgramParameter baseParameter)
{
    return { EditKind::MorphAmount, slot, static_cast<std::uint8_t>(baseParameter) };
}

bool editableSettingsDiffer(const std::optional<Performance>& before, const std::optional<Performance>& after)
{
    if (before.has_value() != after.has_value())
        return true;
    if (! before)
        return false;
    for (const auto& info : performanceSettings()) {
        if (info.access != PerformanceSettingAccess::Editable)
            continue;
        for (auto slot : kAllSlots) {
            if (before->setting(info.id, slot) != after->setting(info.id, slot))
                return true;
            if (! info.perSlot)
                break;
        }
    }
    return false;
}

void EditHistory::record(EditStepId step, EditTarget target, int before, int after)
{
    if (before == after)
        return;

    if (joinableStep == step && ! undoSteps.empty() && undoSteps.back().id == step) {
        auto& changes = undoSteps.back().changes;
        const auto existing = std::find_if(changes.begin(), changes.end(),
                                           [&target](const auto& change) { return change.target == target; });
        if (existing == changes.end()) {
            changes.push_back({ target, before, after });
            return;
        }
        existing->after = after;
        if (existing->after == existing->before) {
            changes.erase(existing);
            if (changes.empty())
                undoSteps.pop_back();
        }
        return;
    }

    redoSteps.clear();
    undoSteps.push_back({ step, { { target, before, after } } });
    if (undoSteps.size() > kMaxSteps)
        undoSteps.pop_front();
    joinableStep = step;
}

std::optional<std::vector<EditChange>> EditHistory::takeUndo()
{
    joinableStep.reset();
    if (undoSteps.empty())
        return std::nullopt;
    auto step = std::move(undoSteps.back());
    undoSteps.pop_back();
    auto changes = step.changes;
    redoSteps.push_back(std::move(step));
    return changes;
}

std::optional<std::vector<EditChange>> EditHistory::takeRedo()
{
    joinableStep.reset();
    if (redoSteps.empty())
        return std::nullopt;
    auto step = std::move(redoSteps.back());
    redoSteps.pop_back();
    auto changes = step.changes;
    undoSteps.push_back(std::move(step));
    return changes;
}

bool EditHistory::forget(const std::function<bool(const EditTarget&)>& replaced)
{
    bool dropped = false;
    const auto dropFrom = [&](auto& steps) {
        for (auto& step : steps)
            dropped = std::erase_if(step.changes, [&](const auto& change) { return replaced(change.target); }) > 0
                      || dropped;
        std::erase_if(steps, [](const auto& step) { return step.changes.empty(); });
    };
    dropFrom(undoSteps);
    dropFrom(redoSteps);
    if (dropped)
        joinableStep.reset();
    return dropped;
}

} // namespace nl2x
