#include "PerformanceEditBuffer.h"

#include "nl2x/HardwareAssumptions.h"

#include <algorithm>

namespace nl2x {

bool PerformanceEditBuffer::isReply(std::span<const std::uint8_t> message)
{
    return message.size() == kPerformanceDumpSize && parsePerformanceDump(message).has_value();
}

void PerformanceEditBuffer::forget()
{
    current.reset();
    readStatus = PerformanceStatus::NotRead;
    unsentBytes.clear();
}

void PerformanceEditBuffer::restore(const std::optional<Performance>& performance)
{
    current = performance;
    readStatus = performance ? PerformanceStatus::Read : PerformanceStatus::NotRead;
    unsentBytes.clear();
}

void PerformanceEditBuffer::applyReply(std::span<const std::uint8_t> reply)
{
    const auto dump = parsePerformanceDump(reply);
    if (! dump) {
        readFailed();
        return;
    }

    auto values = dump->performance.bytes();
    for (const auto& [offset, byte] : unsentBytes)
        values[offset] = byte;
    current = Performance(values);
    readStatus = PerformanceStatus::Read;
}

PerformanceEditResult PerformanceEditBuffer::edit(PerformanceSetting setting, Slot slot, int value)
{
    const auto& info = performanceSettingInfo(setting);
    if (info.access != PerformanceSettingAccess::Editable)
        return PerformanceEditResult::RefusedNotEditable;
    if (! current)
        return PerformanceEditResult::RefusedNotRead;

    store(setting, slot, std::clamp(value, info.minValue, info.maxValue));
    return PerformanceEditResult::Accepted;
}

void PerformanceEditBuffer::restore(PerformanceSetting setting, Slot slot, int value)
{
    if (current && performanceSettingInfo(setting).access == PerformanceSettingAccess::Editable)
        store(setting, slot, value);
}

void PerformanceEditBuffer::store(PerformanceSetting setting, Slot slot, int value)
{
    if (current->setting(setting, slot) == value)
        return;
    current->setSetting(setting, slot, value);
    const auto offset = performanceSettingOffset(setting, slot);
    unsentBytes[offset] = current->bytes()[offset];
}

std::optional<std::vector<std::uint8_t>> PerformanceEditBuffer::takeDueDump(TimeMs timeMs, GlobalChannel globalChannel,
                                                                           const std::array<SlotState, 4>& slots)
{
    if (unsentBytes.empty() || ! current)
        return std::nullopt;
    if (lastDumpMs && timeMs - *lastDumpMs < hardware::kPerformanceDumpIntervalMs)
        return std::nullopt;

    auto sent = *current;
    for (auto slot : kAllSlots) {
        const auto& state = slots[slotIndex(slot)];
        if (state.content == SlotContent::Program && state.program)
            sent.setProgram(slot, *state.program);
    }

    unsentBytes.clear();
    lastDumpMs = timeMs;
    return performanceEditBufferDump(globalChannel, sent.bytes());
}

std::optional<MidiChannel> PerformanceEditBuffer::slotChannel(Slot slot) const
{
    if (! current)
        return std::nullopt;
    return hardware::slotChannelFromPerformance(
        current->bytes()[performanceSettingOffset(PerformanceSetting::SlotChannel, slot)]);
}

SlotLayerArray PerformanceEditBuffer::layer() const
{
    SlotLayerArray layered {};
    if (current)
        for (auto slot : kAllSlots)
            layered[slotIndex(slot)] = current->setting(PerformanceSetting::Layer, slot) != 0;
    return layered;
}

} // namespace nl2x
