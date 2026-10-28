#pragma once

// Nord Lead 2X SysEx framing, nibble coding, request builders and a classifier for incoming
// messages (research §3). Values are never clamped: any byte decodes and encodes back unchanged.

#include "nl2x/ProgramParameters.h"
#include "nl2x/Slot.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace nl2x {

// The Global Channel as carried in SysEx, 0–15: it is the synth's SysEx Device ID [OFF-2X p.105].
// How the synth displays it is in HardwareAssumptions.h.
using GlobalChannel = std::uint8_t;

// F0 33 <dev> 04 <type> <spec> … F7 [OFF-2X p.105], confirmed by every factory file [OFF-FB].
inline constexpr std::uint8_t kSysExStart = 0xF0;
inline constexpr std::uint8_t kSysExEnd = 0xF7;
inline constexpr std::uint8_t kClaviaManufacturerId = 0x33;
inline constexpr std::uint8_t kNordLeadModelId = 0x04;
inline constexpr std::size_t kSysExHeaderSize = 6;
// Header plus the closing F7.
inline constexpr std::size_t kSysExFramingSize = kSysExHeaderSize + 1;

// Parameter counts [OFF-2X p.110]: a Percussion Kit is 8 Programs; a Performance is 4 Programs
// plus 90 Performance settings.
inline constexpr std::size_t kPercussionKitProgramCount = 8;
inline constexpr std::size_t kPercussionKitParameterCount = kPercussionKitProgramCount * kProgramParameterCount;
inline constexpr std::size_t kPerformanceSettingsCount = 90;
inline constexpr std::size_t kPerformanceParameterCount = 4 * kProgramParameterCount + kPerformanceSettingsCount;

// Whole message sizes, verified against [OFF-FB]: 139, 1063, 715.
inline constexpr std::size_t kProgramDumpSize = kSysExFramingSize + 2 * kProgramParameterCount;
inline constexpr std::size_t kPercussionKitDumpSize = kSysExFramingSize + 2 * kPercussionKitParameterCount;
inline constexpr std::size_t kPerformanceDumpSize = kSysExFramingSize + 2 * kPerformanceParameterCount;

// Decoded Performance offsets used by the probe and the simulator [OFF-2X p.112].
inline constexpr std::size_t kPerformanceSlotChannelOffset = 264;
inline constexpr std::size_t kPerformanceKeyboardSplitOffset = 352;

// Each 8-bit signed value becomes two MIDI bytes, low nibble first [OFF-2X p.110].
std::vector<std::uint8_t> encodeNibbles(std::span<const std::int8_t> values);
// Empty when the length is odd or a byte is above 0x0F: such data cannot round-trip.
std::optional<std::vector<std::int8_t>> decodeNibbles(std::span<const std::uint8_t> data);

// A complete message carrying `values` nibble-encoded. `type` and `spec` must be data bytes (< 0x80).
std::vector<std::uint8_t> buildSysEx(GlobalChannel globalChannel, std::uint8_t type, std::uint8_t spec,
                                     std::span<const std::int8_t> values);
// A request: a message with no data.
std::vector<std::uint8_t> buildRequest(GlobalChannel globalChannel, std::uint8_t type, std::uint8_t spec);

// Requests for the message types in research §3. Type codes come from HardwareAssumptions.h.
std::vector<std::uint8_t> slotEditBufferRequest(GlobalChannel globalChannel, Slot slot);
std::vector<std::uint8_t> allControllersRequest(GlobalChannel globalChannel, Slot slot);
std::vector<std::uint8_t> performanceEditBufferRequest(GlobalChannel globalChannel);
// `sysExBank` counts as the manual's SysEx tables do, 1–4 (see HardwareAssumptions.h).
std::vector<std::uint8_t> performanceBankRequest(GlobalChannel globalChannel, int sysExBank, std::uint8_t location);
// A request for a Program (0–98) or Percussion Kit (99–108) in a Bank location. The request type
// for Banks is unknown on a 2X (HardwareAssumptions.h), so the caller names it.
std::vector<std::uint8_t> bankLocationRequest(GlobalChannel globalChannel, std::uint8_t requestType,
                                              std::uint8_t location);

// Dumps.
std::vector<std::uint8_t> programEditBufferDump(GlobalChannel globalChannel, Slot slot,
                                                std::span<const std::int8_t> program);
std::vector<std::uint8_t> performanceEditBufferDump(GlobalChannel globalChannel,
                                                    std::span<const std::int8_t> performance);
// Stored by the synth at `location` in the Bank selected on the synth; `type` is ignored on
// receive [OFF-2X p.78] but must still be a valid Bank dump type.
std::vector<std::uint8_t> bankProgramDump(GlobalChannel globalChannel, std::uint8_t type, std::uint8_t location,
                                          std::span<const std::int8_t> program);

// Classified by size alone: the type byte is unreliable (research §3, [OFF-FB]).
enum class SysExKind : std::uint8_t {
    NotNordLead,      // not F0 33 <dev> 04 … F7
    ProgramDump,      // 139 bytes
    PercussionKitDump, // 1063 bytes
    PerformanceDump,  // 715 bytes
    Other,            // a Nord Lead message of any other size
};

std::string_view sysExKindName(SysExKind kind);

struct ParsedSysEx {
    SysExKind kind = SysExKind::NotNordLead;
    std::size_t size = 0;
    // Valid unless kind is NotNordLead.
    GlobalChannel globalChannel = 0;
    std::uint8_t type = 0;
    std::uint8_t spec = 0;
    // Every data byte decoded, for any Nord Lead message whose data is valid nibbles; empty otherwise.
    std::optional<std::vector<std::int8_t>> values;
};

ParsedSysEx parseSysEx(std::span<const std::uint8_t> message);

} // namespace nl2x
