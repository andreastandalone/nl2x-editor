#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

// A plain view of the synth's Performance (#5): "Read from synth", the Slot Channels read from the
// Performance with a manual override per Slot, the Performance settings the synth receives as
// plain controls (named choices where the manual gives them), and the settings it does not
// receive, shown only. Styled by NordLookAndFeel. It polls the engine's state revision.
class PerformanceView final : public juce::Component, private juce::Timer {
public:
    explicit PerformanceView(NL2XEditorProcessor& owner);
    ~PerformanceView() override;

    void resized() override;

    // One control for one setting of one Slot (or of the whole Performance).
    class SettingControl;

private:
    struct SettingRow {
        const nl2x::PerformanceSettingInfo* info;
        std::unique_ptr<juce::Label> label;
        // Four controls for per-Slot settings, one otherwise; empty for settings shown only.
        std::vector<std::unique_ptr<SettingControl>> controls;
        // For settings shown only.
        std::unique_ptr<juce::Label> valueText;
    };

    void timerCallback() override;
    void refresh();
    void layOutContent();
    SettingRow& addRow(const nl2x::PerformanceSettingInfo& info);
    juce::Label& addHeading(const juce::String& text);

    NL2XEditorProcessor& processor;
    std::uint64_t shownRevision = 0;

    juce::TextButton readButton { "Read from synth" };
    juce::Label statusLabel;

    juce::Viewport viewport;
    juce::Component content;
    std::vector<std::unique_ptr<juce::Label>> headings;
    std::array<juce::Label, 4> slotColumnLabels;
    juce::Label performanceChannelsTitle { {}, "From the Performance" };
    std::array<juce::Label, 4> performanceChannels;
    juce::Label overridesTitle { {}, "Manual override" };
    std::array<juce::ComboBox, 4> overrideBoxes;
    juce::Label sendChannelsTitle { {}, "Knob changes go out on" };
    std::array<juce::Label, 4> sendChannels;
    std::vector<SettingRow> editableRows;
    std::vector<SettingRow> shownOnlyRows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PerformanceView)
};
