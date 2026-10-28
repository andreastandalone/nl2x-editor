#include "nl2x/ProgramParameters.h"

#include <array>
#include <cassert>

namespace nl2x {

namespace {

constexpr std::array kProgramParameters {
    ProgramParameterInfo { ProgramParameter::FilterCutoff, "filterCutoff", "Filter Cutoff", 74, 3, 0, 127, {} },
};

} // namespace

std::span<const ProgramParameterInfo> programParameters()
{
    return kProgramParameters;
}

const ProgramParameterInfo& programParameterInfo(ProgramParameter parameter)
{
    const auto& info = kProgramParameters[static_cast<std::size_t>(parameter)];
    assert(info.id == parameter && "kProgramParameters must be in ProgramParameter order");
    return info;
}

std::string dawParameterId(Slot slot, ProgramParameter parameter)
{
    return std::string(1, slotLetter(slot)) + "_" + std::string(programParameterInfo(parameter).key);
}

std::string dawParameterName(Slot slot, ProgramParameter parameter)
{
    return std::string(1, slotLetter(slot)) + ": " + std::string(programParameterInfo(parameter).name);
}

} // namespace nl2x
