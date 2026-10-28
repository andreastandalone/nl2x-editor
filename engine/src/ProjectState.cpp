#include "nl2x/ProjectState.h"

#include <algorithm>

namespace nl2x {

namespace {

constexpr std::array<std::uint8_t, 4> kMagic { 'N', 'L', '2', 'P' };

constexpr std::uint8_t kContentUnknown = 0;
constexpr std::uint8_t kContentProgram = 1;
constexpr std::uint8_t kContentPercussionKit = 2;

template <std::size_t N>
void appendBytes(std::vector<std::uint8_t>& out, const std::array<std::int8_t, N>& values)
{
    for (auto value : values)
        out.push_back(static_cast<std::uint8_t>(value));
}

// Reads `N` signed bytes at `position`, advancing it; empty when the blob is too short.
template <std::size_t N>
std::optional<std::array<std::int8_t, N>> readBytes(std::span<const std::uint8_t> state, std::size_t& position)
{
    if (state.size() - position < N)
        return std::nullopt;
    std::array<std::int8_t, N> values;
    for (std::size_t i = 0; i < N; ++i)
        values[i] = static_cast<std::int8_t>(state[position + i]);
    position += N;
    return values;
}

} // namespace

std::vector<std::uint8_t> encodeProjectState(const ProjectSounds& sounds)
{
    std::vector<std::uint8_t> out(kMagic.begin(), kMagic.end());
    out.push_back(kProjectStateFormatVersion);
    for (const auto& slot : sounds.slots) {
        if (slot.content == SlotContent::Program && slot.program) {
            out.push_back(kContentProgram);
            appendBytes(out, slot.program->bytes());
        } else {
            out.push_back(slot.content == SlotContent::PercussionKit ? kContentPercussionKit : kContentUnknown);
        }
    }
    out.push_back(sounds.performance ? 1 : 0);
    if (sounds.performance)
        appendBytes(out, sounds.performance->bytes());
    return out;
}

std::optional<ProjectSounds> decodeProjectState(std::span<const std::uint8_t> state)
{
    if (state.size() < kMagic.size() + 1 || ! std::equal(kMagic.begin(), kMagic.end(), state.begin())
        || state[kMagic.size()] != kProjectStateFormatVersion)
        return std::nullopt;

    std::size_t position = kMagic.size() + 1;
    ProjectSounds sounds;
    for (auto& slot : sounds.slots) {
        if (position >= state.size())
            return std::nullopt;
        switch (state[position++]) {
        case kContentUnknown:
            break;
        case kContentPercussionKit:
            slot.content = SlotContent::PercussionKit;
            break;
        case kContentProgram: {
            const auto values = readBytes<kProgramParameterCount>(state, position);
            if (! values)
                return std::nullopt;
            slot.content = SlotContent::Program;
            slot.program = Program(*values);
            break;
        }
        default:
            return std::nullopt;
        }
    }

    if (position >= state.size())
        return std::nullopt;
    const auto hasPerformance = state[position++];
    if (hasPerformance > 1)
        return std::nullopt;
    if (hasPerformance == 1) {
        const auto values = readBytes<kPerformanceParameterCount>(state, position);
        if (! values)
            return std::nullopt;
        sounds.performance = Performance(*values);
    }
    if (position != state.size())
        return std::nullopt;
    return sounds;
}

} // namespace nl2x
