add_library(nl2x_sim STATIC
    sim/SimulatedNordLead2X.cpp)
target_include_directories(nl2x_sim PUBLIC sim)
target_link_libraries(nl2x_sim PUBLIC nl2x_engine)
target_compile_options(nl2x_sim PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror)

add_executable(nl2x_tests
    FactoryBankTest.cpp
    ParameterToCcTest.cpp
    PerformanceCodecTest.cpp
    PerformanceTest.cpp
    ProbeLogTest.cpp
    ProbeScriptsTest.cpp
    ProgramCodecTest.cpp
    ReadSlotsTest.cpp
    SimulatedNordLead2XTest.cpp
    SysExTest.cpp)
target_link_libraries(nl2x_tests PRIVATE nl2x_sim nl2x_probe nl2x_engine Catch2::Catch2WithMain)
# The official factory files are not in the repository (ADR 0002); FactoryBankTest skips itself
# when they are not present here.
set(NL2X_FACTORY_BANK_DIR "${PROJECT_SOURCE_DIR}/docs/research/factory-bank" CACHE PATH
    "Folder with the official Nord Lead 2X factory .syx files, for FactoryBankTest")
target_compile_definitions(nl2x_tests PRIVATE NL2X_FACTORY_BANK_DIR="${NL2X_FACTORY_BANK_DIR}")
target_compile_options(nl2x_tests PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror)

include(Catch)
catch_discover_tests(nl2x_tests)
