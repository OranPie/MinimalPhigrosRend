cmake_minimum_required(VERSION 3.16)
project(phigros_core CXX C)

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

# --- Vendor include directories ---
set(VENDOR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor")

# --- Core library (math + types + parsers) ---
add_library(phigros_core_lib STATIC
    src/chart/official.cpp
    src/chart/rpe.cpp
    src/chart/pec.cpp
)
target_include_directories(phigros_core_lib PUBLIC include)
target_link_libraries(phigros_core_lib PUBLIC nlohmann_json::nlohmann_json)

# --- Main executable (headless) ---
add_executable(phigros_core src/main.cpp)
target_link_libraries(phigros_core PRIVATE phigros_core_lib)

# --- Test executables ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_easing.cpp")
    add_executable(test_easing tests/test_easing.cpp)
    target_link_libraries(test_easing PRIVATE phigros_core_lib)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_chart.cpp")
    add_executable(verify_chart tests/verify_chart.cpp)
    target_link_libraries(verify_chart PRIVATE phigros_core_lib)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_engine.cpp")
    add_executable(test_engine tests/test_engine.cpp)
    target_link_libraries(test_engine PRIVATE phigros_core_lib)
endif()

# --- Vendor libraries (stb, miniz, miniaudio) ---
add_library(vendor_stb STATIC src/vendor/vendor_impl.cpp)
target_include_directories(vendor_stb PUBLIC "${VENDOR_DIR}/stb")

add_library(vendor_miniz STATIC
    "${VENDOR_DIR}/miniz/miniz.c"
    "${VENDOR_DIR}/miniz/miniz_tdef.c"
    "${VENDOR_DIR}/miniz/miniz_tinfl.c"
    "${VENDOR_DIR}/miniz/miniz_zip.c"
)
target_include_directories(vendor_miniz PUBLIC "${VENDOR_DIR}/miniz")

add_library(vendor_miniaudio STATIC src/vendor/audio_impl.cpp)
target_include_directories(vendor_miniaudio PUBLIC "${VENDOR_DIR}/miniaudio")
target_link_libraries(vendor_miniaudio PUBLIC dl pthread m)

# --- SDL2 ---
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

# --- Render executable (SDL2 + full visual pipeline) ---
add_executable(phigros_render src/app/main.cpp)
target_include_directories(phigros_render PRIVATE
    ${SDL2_INCLUDE_DIRS}
    "${VENDOR_DIR}/stb"
    "${VENDOR_DIR}/miniz"
    "${VENDOR_DIR}/miniaudio"
)
target_link_libraries(phigros_render PRIVATE
    phigros_core_lib
    vendor_stb
    vendor_miniz
    vendor_miniaudio
    ${SDL2_LIBRARIES}
)
target_compile_options(phigros_render PRIVATE ${SDL2_CFLAGS_OTHER})
