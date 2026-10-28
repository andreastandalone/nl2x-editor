#include "nl2x/SysEx.h"

#include "nl2x/HardwareAssumptions.h"

#include <cassert>

namespace nl2x {

std::vector<std::uint8_t> encodeNibbles(std::span<const std::int8_t> values)
{
    std::vector<std::uint8_t> data;
    data.reserve(2 * values.size());
    for (auto value : values) {
        const auto byte = static_cast<std::uint8_t>(value);
        data.push_back(static_cast<std::uint8_t>(byte & 0x0F));
        data.push_back(static_cast<std::uint8_t>(byte >> 4));
    }
    return data;
}

std::optional<std::vector<std::int8_t>> decodeNibbles(std::span<const std::uint8_t> data)
{
    if (data.size() % 2 != 0)
        return std::nullopt;

    std::vector<std::int8_t> values;
    values.reserve(data.size() / 2);
    for (std::size_t i = 0; i < data.size(); i += 2) {
        if (data[i] > 0x0F || data[i + 1] > 0x0F)
            return std::nullopt;
        values.push_back(static_cast<std::int8_t>(static_cast<std::uint8_t>(data[i] | (data[i + 1] << 4))));
    }
    return values;
}

std::vector<std::uint8_t> buildSysEx(GlobalChannel globalChannel, std::uint8_t type, std::uint8_t spec,
                                     std::span<const std::int8_t> values)
{
    assert(globalChannel < 0x80 && type < 0x80 && spec < 0x80);
    std::vector<std::uint8_t> message { kSysExStart, kClaviaManufacturerId, globalChannel, kNordLeadModelId, type, spec };
    const auto data = encodeNibbles(values);
    message.insert(message.end(), data.begin(), data.end());
    message.push_back(kSysExEnd);
    return message;
}

std::vector<std::uint8_t> buildRequest(GlobalChannel globalChannel, std::uint8_t type, std::uint8_t spec)
{
    return buildSysEx(globalChannel, type, spec, {});
}

std::vector<std::uint8_t> slotEditBufferRequest(GlobalChannel globalChannel, Slot slot)
{
    return buildRequest(globalChannel, hardware::kSlotEditBufferRequestType, static_cast<std::uint8_t>(slotIndex(slot)));
}

std::vector<std::uint8_t> allControllersRequest(GlobalChannel globalChannel, Slot slot)
{
    return buildRequest(globalChannel, hardware::kAllControllersRequestType, static_cast<std::uint8_t>(slotIndex(slot)));
}

std::vector<std::uint8_t> performanceEditBufferRequest(GlobalChannel globalChannel)
{
    return buildRequest(globalChannel, hardware::kPerformanceEditBufferRequestType, 0);
}

std::vector<std::uint8_t> performanceBankRequest(GlobalChannel globalChannel, int sysExBank, std::uint8_t location)
{
    assert(sysExBank >= 1 && sysExBank <= 4);
    return buildRequest(globalChannel,
                        static_cast<std::uint8_t>(hardware::kFirstPerformanceBankRequestType + sysExBank - 1), location);
}

std::vector<std::uint8_t> bankLocationRequest(GlobalChannel globalChannel, std::uint8_t requestType,
                                              std::uint8_t location)
{
    return buildRequest(globalChannel, requestType, location);
}

std::vector<std::uint8_t> programEditBufferDump(GlobalChannel globalChannel, Slot slot,
                                                std::span<const std::int8_t> program)
{
    assert(program.size() == kProgramParameterCount);
    return buildSysEx(globalChannel, hardware::kProgramEditBufferDumpType, static_cast<std::uint8_t>(slotIndex(slot)),
                      program);
}

std::vector<std::uint8_t> performanceEditBufferDump(GlobalChannel globalChannel,
                                                    std::span<const std::int8_t> performance)
{
    assert(performance.size() == kPerformanceParameterCount);
    return buildSysEx(globalChannel, hardware::kPerformanceEditBufferDumpType, 0, performance);
}

std::vector<std::uint8_t> bankProgramDump(GlobalChannel globalChannel, std::uint8_t type, std::uint8_t location,
                                          std::span<const std::int8_t> program)
{
    assert(program.size() == kProgramParameterCount);
    assert(type >= hardware::kFirstProgramBankDumpType && type <= hardware::kLastProgramBankDumpType);
    return buildSysEx(globalChannel, type, location, program);
}

std::string_view sysExKindName(SysExKind kind)
{
    switch (kind) {
    case SysExKind::NotNordLead: return "not a Nord Lead message";
    case SysExKind::ProgramDump: return "Program dump";
    case SysExKind::PercussionKitDump: return "Percussion Kit dump";
    case SysExKind::PerformanceDump: return "Performance dump";
    case SysExKind::Other: return "other Nord Lead message";
    }
    return "unknown";
}

ParsedSysEx parseSysEx(std::span<const std::uint8_t> message)
{
    ParsedSysEx parsed;
    parsed.size = message.size();
    if (message.size() < kSysExFramingSize || message[0] != kSysExStart || message[1] != kClaviaManufacturerId
        || message[3] != kNordLeadModelId || message.back() != kSysExEnd)
        return parsed;

    parsed.globalChannel = message[2];
    parsed.type = message[4];
    parsed.spec = message[5];
    parsed.values = decodeNibbles(message.subspan(kSysExHeaderSize, message.size() - kSysExFramingSize));

    switch (message.size()) {
    case kProgramDumpSize: parsed.kind = SysExKind::ProgramDump; break;
    case kPercussionKitDumpSize: parsed.kind = SysExKind::PercussionKitDump; break;
    case kPerformanceDumpSize: parsed.kind = SysExKind::PerformanceDump; break;
    default: parsed.kind = SysExKind::Other; break;
    }
    return parsed;
}

} // namespace nl2x
