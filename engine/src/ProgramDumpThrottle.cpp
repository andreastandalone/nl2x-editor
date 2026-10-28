#include "ProgramDumpThrottle.h"

#include "nl2x/HardwareAssumptions.h"

namespace nl2x {

void ProgramDumpThrottle::edited(Slot slot, ProgramParameter parameter, std::int8_t byte)
{
    perSlot[slotIndex(slot)].unsent[static_cast<std::size_t>(parameter)] = byte;
}

void ProgramDumpThrottle::applyUnsentEdits(Slot slot, Program& program) const
{
    for (const auto& [offset, byte] : perSlot[slotIndex(slot)].unsent)
        program.setValue(static_cast<ProgramParameter>(offset), byte);
}

void ProgramDumpThrottle::dropAllUnsentEdits()
{
    for (auto slot : kAllSlots)
        dropUnsentEdits(slot);
}

bool ProgramDumpThrottle::isDue(Slot slot, TimeMs timeMs) const
{
    const auto& dumps = perSlot[slotIndex(slot)];
    if (dumps.unsent.empty())
        return false;
    return ! dumps.lastDumpMs || timeMs - *dumps.lastDumpMs >= hardware::kProgramDumpIntervalMs;
}

void ProgramDumpThrottle::sent(Slot slot, TimeMs timeMs)
{
    auto& dumps = perSlot[slotIndex(slot)];
    dumps.unsent.clear();
    dumps.lastDumpMs = timeMs;
}

} // namespace nl2x
