#include "SavedProjectSounds.h"

namespace saved_project_sounds {

namespace {

const juce::Identifier kElement { "ProjectSounds" };
const juce::Identifier kVersion { "version" };

} // namespace

void write(juce::XmlElement& state, const juce::MemoryBlock& blob)
{
    if (blob.isEmpty())
        return;
    auto* element = state.createNewChildElement(kElement.toString());
    element->setAttribute(kVersion, kElementVersion);
    element->addTextElement(blob.toBase64Encoding());
}

std::optional<juce::MemoryBlock> take(juce::XmlElement& state)
{
    auto* element = state.getChildByName(kElement.toString());
    if (element == nullptr)
        return std::nullopt;
    std::optional<juce::MemoryBlock> blob;
    if (element->getIntAttribute(kVersion, 0) == kElementVersion) {
        juce::MemoryBlock decoded;
        if (decoded.fromBase64Encoding(element->getAllSubText().trim()) && ! decoded.isEmpty())
            blob = std::move(decoded);
    }
    state.removeChildElement(element, true);
    return blob;
}

} // namespace saved_project_sounds
