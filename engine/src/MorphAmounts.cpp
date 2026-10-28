#include "nl2x/MorphAmounts.h"

#include <array>
#include <cassert>

namespace nl2x {

namespace {

constexpr std::size_t kMorphableCount = 25;

constexpr std::array<ProgramParameter, kMorphableCount> makeMorphable()
{
    std::array<ProgramParameter, kMorphableCount> parameters {};
    for (std::size_t i = 0; i < kMorphableCount; ++i)
        parameters[i] = static_cast<ProgramParameter>(i);
    return parameters;
}

// Offsets 0–24 [OFF-2X p.111]; ProgramParameters.cpp pairs each with its Morph Amount.
constexpr auto kMorphable = makeMorphable();

std::string slotName(Slot slot)
{
    return std::string("Slot ") + slotLetter(slot);
}

} // namespace

std::span<const ProgramParameter> morphableParameters()
{
    return kMorphable;
}

std::optional<ProgramParameter> morphAmountOf(ProgramParameter baseParameter)
{
    return programParameterInfo(baseParameter).morphAmount;
}

int morphAmountValue(const Program& program, ProgramParameter baseParameter)
{
    const auto morph = morphAmountOf(baseParameter);
    assert(morph);
    return program.value(*morph);
}

std::string morphDawParameterId(Slot slot, ProgramParameter baseParameter)
{
    return dawParameterId(slot, baseParameter) + "Morph";
}

std::string morphDawParameterName(Slot slot, ProgramParameter baseParameter)
{
    return dawParameterName(slot, baseParameter) + " Morph";
}

std::string morphEditRefusalText(MorphEditResult result, Slot slot)
{
    switch (result) {
    case MorphEditResult::Accepted:
        return {};
    case MorphEditResult::RefusedNotMorphable:
        return "This parameter has no Morph Amount.";
    case MorphEditResult::RefusedSlotNotRead:
        return slotName(slot) + " has not been read from the synth yet: read it first, so that a Program dump does "
                                "not overwrite its sound with guesses.";
    case MorphEditResult::RefusedSlotPlaysPercussionKit:
        return slotName(slot) + " is playing a Percussion Kit: Morph Amounts can only be edited on a Slot playing a "
                                "Program.";
    case MorphEditResult::RefusedNotConnected:
        return "Not connected to the synth: nothing was sent.";
    }
    return {};
}

} // namespace nl2x
