cmake_minimum_required(VERSION 3.25)

# Must be set before project(): they configure the toolchain.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

option(NL2X_BUILD_PLUGIN "Build the JUCE plugin (fetches JUCE). OFF builds only the engine and the tests." ON)
option(NL2X_BUILD_TESTS "Build the Catch2 tests and the simulated Nord Lead 2X" ON)

# The released plugin is a Universal binary. Engine-only builds stay native so they are quick.
if(NL2X_BUILD_PLUGIN AND NOT DEFINED CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Architectures to build")
endif()

# The version lives here and nowhere else. VERSION is numeric because the bundles' version fields
# (CFBundleShortVersionString, the AU version) must be; NL2X_VERSION_LABEL marks a pre-release such
# as "beta.1" and is empty for a final release. The release tag is "v" + NL2X_VERSION, for example
# v0.1.0-beta.1; release.yml refuses a tag that does not match (docs/release-process.md).
project(NL2XEditor VERSION 0.1.0 LANGUAGES C CXX)
set(NL2X_VERSION_LABEL "beta.1")

if(NL2X_VERSION_LABEL)
    set(NL2X_VERSION "${PROJECT_VERSION}-${NL2X_VERSION_LABEL}")
else()
    set(NL2X_VERSION "${PROJECT_VERSION}")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
include(cmake/Dependencies.cmake)

add_subdirectory(engine)
add_subdirectory(probe)

if(NL2X_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(NL2X_BUILD_PLUGIN)
    add_subdirectory(plugin)
    # A development tool: paints the editor into a PNG (README, "Building"). It needs the simulator.
    if(NL2X_BUILD_TESTS)
        add_subdirectory(tools/render-editor)
    endif()
endif()
