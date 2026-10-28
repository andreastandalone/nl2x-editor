#pragma once

#include <juce_core/juce_core.h>

#include <optional>

// The project's sounds (#8, spec #1 user stories 43–47): the engine's project state blob
// (nl2x/ProjectState.h), opaque to the shell, kept as one child element of the plugin state next
// to the DAW parameters, the selection and the Slot Channel overrides:
// <ProjectSounds version="1">base64</ProjectSounds>. `version` is the shell's own element
// version; the blob carries the engine's format version, which the engine checks on load.
namespace saved_project_sounds {

inline constexpr int kElementVersion = 1;

// Writes nothing for an empty blob.
void write(juce::XmlElement& state, const juce::MemoryBlock& blob);
// Reads and removes the element, so that it never reaches the parameter tree. Empty when absent,
// of another element version, or not valid base64: the engine then reads the synth as usual.
std::optional<juce::MemoryBlock> take(juce::XmlElement& state);

} // namespace saved_project_sounds
