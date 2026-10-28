# The hardware discovery probe (#3): plain C++20 like the engine, shown only by the standalone app.
add_library(nl2x_probe STATIC
    src/ProbeLog.cpp
    src/ProbeSession.cpp
    src/Scripts.cpp)

target_include_directories(nl2x_probe PUBLIC include)
target_link_libraries(nl2x_probe PUBLIC nl2x_engine)
target_compile_options(nl2x_probe PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror)
