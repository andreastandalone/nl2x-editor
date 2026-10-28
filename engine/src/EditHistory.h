#pragma once

#include "nl2x/CcParameters.h"
#include "nl2x/EditStep.h"
#include "nl2x/Performance.h"
#include "nl2x/PerformanceSettings.h"
#include "nl2x/ProgramParameters.h"
#include "nl2x/Slot.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <vector>

namespace nl2x {

// What an undoable edit changed. A new kind of edit adds a kind here, a case in Engine::applyEdit,
// and an Engine::recordUserEdit call in its setter; nothing else.
enum class EditKind : std::uint8_t {
    // A CcParameter of a Slot's Program Edit Buffer; `id` is the CcParameter.
    CcParameter,
    // A PerformanceSetting of the Performance Edit Buffer; `id` is the setting, `slot` is Slot A
    // for settings that are not per Slot.
    PerformanceSetting,
    // The Morph Amount of a Slot's Program; `id` is the morphable base ProgramParameter.
    MorphAmount,
};

// One value in the Synth model that the user can edit.
struct EditTarget {
    EditKind kind;
    Slot slot;
    std::uint8_t id;

    // Whether the value lives in `slot`'s Program Edit Buffer; otherwise in the Performance's.
    bool inSlotEditBuffer() const { return kind != EditKind::PerformanceSetting; }

    friend bool operator==(const EditTarget&, const EditTarget&) = default;
};

EditTarget ccParameterTarget(Slot slot, CcParameter parameter);
EditTarget performanceSettingTarget(PerformanceSetting setting, Slot slot);
EditTarget morphAmountTarget(Slot slot, ProgramParameter baseParameter);

// Whether two copies of the Performance Edit Buffer differ in a setting the user can edit: the
// Programs and the read-only bytes inside it do not touch the history of Performance edits.
bool editableSettingsDiffer(const std::optional<Performance>& before, const std::optional<Performance>& after);

struct EditChange {
    EditTarget target;
    int before;
    int after;
};

// The undo and redo steps of the user's edits (#14). Private to the engine; Engine decides what
// is recorded and applies the values.
class EditHistory {
public:
    static constexpr std::size_t kMaxSteps = 200;

    // The user's edit in `step` changed `target` from `before` to `after`. Joins the latest step
    // when it is `step` and nothing was undone or redone since (a drag back to where it started
    // leaves nothing); otherwise starts a new step, which empties the redo side and, beyond
    // kMaxSteps, drops the oldest step.
    void record(EditStepId step, EditTarget target, int before, int after);

    bool canUndo() const { return ! undoSteps.empty(); }
    bool canRedo() const { return ! redoSteps.empty(); }

    // The changes of the step to undo, in the order they were made; the step moves to the redo
    // side. Empty when there is nothing to undo.
    std::optional<std::vector<EditChange>> takeUndo();
    // The changes of the step to redo; the step moves back to the undo side.
    std::optional<std::vector<EditChange>> takeRedo();

    // Drops the changes of every target `replaced` names, from both sides, and the steps left
    // empty: their values were replaced from outside the history. True if anything was dropped.
    bool forget(const std::function<bool(const EditTarget&)>& replaced);

private:
    struct Step {
        EditStepId id;
        std::vector<EditChange> changes;
    };

    std::deque<Step> undoSteps;
    std::vector<Step> redoSteps;
    // The step `record` may still join.
    std::optional<EditStepId> joinableStep;
};

} // namespace nl2x
