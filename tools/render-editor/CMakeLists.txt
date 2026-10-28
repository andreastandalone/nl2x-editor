# nl2x_render_editor: a development tool that paints the editor into a PNG without opening a
# window, so that the GUI can be looked at (by people and agents) without launching the app. Built
# with the plugin and the tests (it talks to the simulated synth); never shipped or installed.

add_executable(nl2x_render_editor RenderEditor.cpp)
# The plugin's shared code already holds the JUCE modules: compile against them with the same
# include paths and settings, without linking (and so compiling) the modules a second time.
target_include_directories(nl2x_render_editor PRIVATE
    ${PROJECT_SOURCE_DIR}/plugin
    $<TARGET_PROPERTY:NL2XEditor,INCLUDE_DIRECTORIES>)
target_compile_definitions(nl2x_render_editor PRIVATE $<TARGET_PROPERTY:NL2XEditor,COMPILE_DEFINITIONS>)
target_link_libraries(nl2x_render_editor
    PRIVATE
        NL2XEditor
        nl2x_sim
        "-framework Accelerate"
        "-framework AudioToolbox"
        "-framework Cocoa"
        "-framework CoreAudio"
        "-framework CoreAudioKit"
        "-framework CoreMIDI"
        "-framework DiscRecording"
        "-framework IOKit"
        "-framework Metal"
        "-framework MetalKit"
        "-framework QuartzCore"
        "-framework Security"
        "-framework UniformTypeIdentifiers"
        juce::juce_recommended_config_flags)
