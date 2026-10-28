add_library(nl2x_sim STATIC
    sim/SimulatedNordLead2X.cpp)
target_include_directories(nl2x_sim PUBLIC sim)
target_link_libraries(nl2x_sim PUBLIC nl2x_engine)
target_compile_options(nl2x_sim PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror)

add_executable(nl2x_tests
    ParameterToCcTest.cpp
    SimulatedNordLead2XTest.cpp)
target_link_libraries(nl2x_tests PRIVATE nl2x_sim nl2x_engine Catch2::Catch2WithMain)
target_compile_options(nl2x_tests PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror)

include(Catch)
catch_discover_tests(nl2x_tests)
