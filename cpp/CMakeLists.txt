cmake_minimum_required(VERSION 3.16)
project(phigros_core CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- nlohmann/json via FetchContent ---
include(FetchContent)
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(json)

# --- Core library (math + types + parsers) ---
add_library(phigros_core_lib STATIC
    src/chart/official.cpp
    src/chart/rpe.cpp
    src/chart/pec.cpp
)
target_include_directories(phigros_core_lib PUBLIC include)
target_link_libraries(phigros_core_lib PUBLIC nlohmann_json::nlohmann_json)

# --- Main executable ---
add_executable(phigros_core src/main.cpp)
target_link_libraries(phigros_core PRIVATE phigros_core_lib)

# --- Test executable ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_easing.cpp")
    add_executable(test_easing tests/test_easing.cpp)
    target_link_libraries(test_easing PRIVATE phigros_core_lib)
endif()
