#pragma once

#include "nl2x/Selection.h"

#include <juce_core/juce_core.h>

// The selection kept in the project (#10, spec #1 user story 33): which Bank and Program each Slot
// was selected from, and the Performance. Informational, never a DAW parameter; restoring it sends
// nothing. Stored as one child element of the plugin state:
// <Selection slotA="program 3 42" slotC="kit 1 5" performance="2 15"/>.
namespace saved_selection {

void write(juce::XmlElement& state, const nl2x::SelectionState& selection);
// Reads and removes the element, so that it never reaches the parameter tree; empty when absent.
nl2x::SelectionState take(juce::XmlElement& state);

} // namespace saved_selection
