#include "SavedSelection.h"

namespace saved_selection {

namespace {

const juce::Identifier kElement { "Selection" };
const juce::Identifier kPerformance { "performance" };

juce::Identifier slotAttribute(nl2x::Slot slot)
{
    return "slot" + juce::String::charToString(nl2x::slotLetter(slot));
}

} // namespace

void write(juce::XmlElement& state, const nl2x::SelectionState& selection)
{
    auto* element = state.createNewChildElement(kElement.toString());
    for (auto slot : nl2x::kAllSlots)
        if (const auto& program = selection.programs[nl2x::slotIndex(slot)]) {
            const juce::String kind = program->kind == nl2x::ProgramSelection::Kind::PercussionKit ? "kit" : "program";
            element->setAttribute(slotAttribute(slot),
                                  kind + " " + juce::String(program->bank) + " " + juce::String(program->number));
        }
    if (const auto& performance = selection.performance)
        element->setAttribute(kPerformance, juce::String(performance->bank) + " " + juce::String(performance->number));
}

nl2x::SelectionState take(juce::XmlElement& state)
{
    nl2x::SelectionState selection;
    auto* element = state.getChildByName(kElement.toString());
    if (element == nullptr)
        return selection;

    for (auto slot : nl2x::kAllSlots) {
        juce::StringArray parts;
        parts.addTokens(element->getStringAttribute(slotAttribute(slot)), " ", {});
        if (parts.size() != 3 || (parts[0] != "program" && parts[0] != "kit"))
            continue;
        nl2x::ProgramSelection program;
        program.kind = parts[0] == "kit" ? nl2x::ProgramSelection::Kind::PercussionKit
                                         : nl2x::ProgramSelection::Kind::Program;
        program.bank = parts[1].getIntValue();
        program.number = parts[2].getIntValue();
        // The engine ignores selections that do not exist.
        selection.programs[nl2x::slotIndex(slot)] = program;
    }
    juce::StringArray parts;
    parts.addTokens(element->getStringAttribute(kPerformance), " ", {});
    if (parts.size() == 2)
        selection.performance = nl2x::PerformanceSelection { parts[0].getIntValue(), parts[1].getIntValue() };

    state.removeChildElement(element, true);
    return selection;
}

} // namespace saved_selection
