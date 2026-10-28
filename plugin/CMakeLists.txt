# The JUCE shell: a thin layer that connects the engine to the DAW, the GUI and the MIDI ports.

juce_add_plugin(NL2XEditor
    PRODUCT_NAME "NL2X Editor"
    COMPANY_NAME "andreastandalone"
    BUNDLE_ID "io.github.andreastandalone.nl2x-editor"
    DESCRIPTION "An editor for the Nord Lead 2X"
    PLUGIN_MANUFACTURER_CODE Astd
    PLUGIN_CODE Nl2x
    FORMATS VST3 AU Standalone
    # An Instrument, because Ableton Live does not host VST3 MIDI effects (spec #1). It keeps a
    # silent stereo output: some hosts refuse or mishandle instruments without an output bus.
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    # Never install into ~/Library during a build; tools/ and CI copy the bundles explicitly.
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(NL2XEditor PRIVATE
    ArcKnob.cpp
    ConnectionCheckView.cpp
    ConnectionIndicator.cpp
    LibraryPanel.cpp
    MemoryPanel.cpp
    MidiPortSelector.cpp
    MidiPorts.cpp
    PerformanceView.cpp
    PluginEditor.cpp
    PluginProcessor.cpp
    ProbeWindow.cpp
    SavedSelection.cpp
    SelectorBar.cpp
    SlotView.cpp
    SynthControl.cpp)

target_compile_definitions(NL2XEditor PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(NL2XEditor
    PRIVATE
        nl2x_engine
        nl2x_probe
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
