// The Engine's edits without a CC (#7): Morph Amounts, and the ring-mod bit when CC35 does not
// reach it. They reach the synth as whole Program dumps to the Slot's Edit Buffer (type 0, spec =
// Slot), never as Bank dumps, throttled per Slot by ProgramDumpThrottle. Kept apart from Engine.cpp.

#include "nl2x/Engine.h"

#include "EditHistory.h"
#include "PerformanceEditBuffer.h"
#include "ProgramDumpThrottle.h"
#include "SynthConnection.h"

#include "nl2x/HardwareAssumptions.h"

#include <algorithm>

namespace nl2x {

namespace {

constexpr std::uint8_t kControlChange = 0xB0;

} // namespace

MorphEditResult Engine::setMorphAmount(Slot slot, ProgramParameter baseParameter, int amount, TimeMs timeMs)
{
    const auto morph = morphAmountOf(baseParameter);
    if (! morph)
        return MorphEditResult::RefusedNotMorphable;
    auto& state = slots[slotIndex(slot)];
    if (state.content == SlotContent::PercussionKit)
        return MorphEditResult::RefusedSlotPlaysPercussionKit;
    if (state.content != SlotContent::Program || ! state.program)
        return MorphEditResult::RefusedSlotNotRead;
    if (! foundGlobalChannel)
        return MorphEditResult::RefusedNotConnected;

    // Only a user value is brought into range; a byte read from the synth is never touched.
    const auto value = std::clamp(amount, kMorphAmountMin, kMorphAmountMax);
    if (const auto before = state.program->value(*morph); before != value) {
        state.program->setValue(*morph, value);
        recordUserEdit(morphAmountTarget(slot, baseParameter), before, value);
        programDumps->edited(slot, *morph, static_cast<std::int8_t>(value));
        changed();
    }
    sendDueProgramDumps(timeMs);
    return MorphEditResult::Accepted;
}

void Engine::sendDueProgramDumps(TimeMs timeMs)
{
    // Dumps wait while another tool talks to the synth, while the synth's memory is being written,
    // and until no reply is expected.
    if (! foundGlobalChannel || suspended || memory->holdsEditBufferDumps())
        return;
    for (auto slot : kAllSlots) {
        if (! programDumps->isDue(slot, timeMs))
            continue;
        const auto& state = slots[slotIndex(slot)];
        if (state.content != SlotContent::Program || ! state.program) {
            programDumps->dropUnsentEdits(slot);
            continue;
        }
        // Each dump keeps the usual gap after it: a second Slot's dump follows on a later tick.
        if (! connection->isIdle(timeMs))
            return;
        // The whole Edit Buffer as the engine holds it: CC edits and bytes read from the synth
        // outside their documented range go out unchanged.
        connection->sendWithoutReply(programEditBufferDump(*foundGlobalChannel, slot, state.program->bytes()), timeMs);
        programDumps->sent(slot, timeMs);
    }
}

void Engine::dropUnsentProgramEdits(const std::array<bool, 4>& slotsReplaced)
{
    for (auto slot : kAllSlots)
        if (slotsReplaced[slotIndex(slot)])
            programDumps->dropUnsentEdits(slot);
}

void Engine::setRingModSyncWithoutRingModCc(Slot slot, int value)
{
    constexpr auto ccBits = hardware::ringModSyncCcBits(false);
    const auto newValue = value & hardware::kRingModSyncBits;
    auto& program = slots[slotIndex(slot)].program;
    std::optional<int> before;
    if (program)
        before = ccParameterValue(*program, CcParameter::RingModSync);

    if (program && *before != newValue) {
        setCcParameterValue(*program, CcParameter::RingModSync, newValue);
        recordUserEdit(ccParameterTarget(slot, CcParameter::RingModSync), *before,
                       ccParameterValue(*program, CcParameter::RingModSync));
        changed();
        // The ring-mod bit has no CC: it travels with the Slot's next Program dump. A Kit Slot or
        // an unread one has no Edit Buffer to dump, so there only the sync bit reaches the synth.
        if (((*before ^ newValue) & hardware::kRingModSyncBits & ~ccBits) != 0) {
            const auto byte = ccParameterInfo(CcParameter::RingModSync).byte;
            programDumps->edited(slot, byte, static_cast<std::int8_t>(program->value(byte)));
        }
    }

    // The sync bit by CC35, unless the Edit Buffer shows it unchanged.
    if (before && ((*before ^ newValue) & ccBits) == 0)
        return;
    const auto channel = slotChannel(slot);
    if (! channel)
        return;
    const std::array<std::uint8_t, 3> message { static_cast<std::uint8_t>(kControlChange | *channel),
                                                hardware::kRingModSyncCc,
                                                static_cast<std::uint8_t>(newValue & ccBits) };
    output.send(message);
}

void Engine::applySyncOnlyControlChange(MidiChannel channel, std::uint8_t data)
{
    constexpr auto ccBits = hardware::ringModSyncCcBits(false);
    // A value the sync bit cannot hold is ignored.
    if (data > ccBits)
        return;

    SlotChannelArray channels;
    for (auto slot : kAllSlots)
        channels[slotIndex(slot)] = slotChannel(slot);
    for (auto slot : slotsReceivingOn(channel, channels, performanceBuffer->layer())) {
        auto& program = slots[slotIndex(slot)].program;
        if (! program)
            continue;
        const auto before = ccParameterValue(*program, CcParameter::RingModSync);
        // The ring-mod bit stays as it is: CC35 does not carry it.
        const auto value = (before & ~ccBits) | data;
        if (value != before) {
            setCcParameterValue(*program, CcParameter::RingModSync, value);
            changed();
        }
    }
}

} // namespace nl2x
