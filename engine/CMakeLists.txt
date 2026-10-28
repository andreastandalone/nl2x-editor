# The engine: plain C++20, no JUCE, no platform headers (CODING_STANDARDS.md).
add_library(nl2x_engine STATIC
    src/Engine.cpp
    src/Program.cpp
    src/ProgramParameters.cpp
    src/Slot.cpp
    src/SynthConnection.cpp
    src/SysEx.cpp)

target_include_directories(nl2x_engine PUBLIC include)
target_compile_features(nl2x_engine PUBLIC cxx_std_20)
target_compile_options(nl2x_engine PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror)
