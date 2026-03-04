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

# --- SDL3 via FetchContent ---
option(USE_SDL3 "Build with SDL3 instead of SDL2" ON)
option(USE_BGFX "Build with bgfx GPU renderer" ON)

if(USE_SDL3 AND NOT EMSCRIPTEN)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG        release-3.2.8
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE
    )
    FetchContent_MakeAvailable(SDL3)
    set(SDL_TARGET SDL3::SDL3-static)
    set(SDL_VERSION 3)
elseif(EMSCRIPTEN)
    # Emscripten has built-in SDL2 port
    set(SDL_VERSION 2)
    set(EMSCRIPTEN_SDL_FLAGS "-s USE_SDL=2")
else()
    # Fallback to system SDL2
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED sdl2)
    set(SDL_VERSION 2)
endif()

# --- bgfx via FetchContent ---
if(USE_BGFX AND NOT EMSCRIPTEN)
    set(BGFX_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BGFX_INSTALL OFF CACHE BOOL "" FORCE)
    set(BGFX_CUSTOM_TARGETS OFF CACHE BOOL "" FORCE)
    set(BX_AMALGAMATED OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        bgfx
        GIT_REPOSITORY https://github.com/bkaradzic/bgfx.cmake.git
        GIT_TAG        v1.140.9174-515
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE
    )
    FetchContent_MakeAvailable(bgfx)
    set(HAS_BGFX TRUE)
endif()

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
    target_link_libraries(test_engine PRIVATE phigros_core_lib vendor_miniz)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_parser.cpp")
    add_executable(test_parser tests/test_parser.cpp)
    target_link_libraries(test_parser PRIVATE phigros_core_lib vendor_miniz)
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
if(NOT EMSCRIPTEN)
    target_link_libraries(vendor_miniaudio PUBLIC dl pthread m)
endif()

# --- Render executable ---
add_executable(phigros_render src/app/main.cpp)
target_include_directories(phigros_render PRIVATE
    "${VENDOR_DIR}/stb"
    "${VENDOR_DIR}/miniz"
    "${VENDOR_DIR}/miniaudio"
)

if(SDL_VERSION EQUAL 3)
    target_link_libraries(phigros_render PRIVATE
        phigros_core_lib vendor_stb vendor_miniz vendor_miniaudio
        ${SDL_TARGET}
    )
    target_compile_definitions(phigros_render PRIVATE PHIGROS_SDL3=1)
elseif(EMSCRIPTEN)
    target_link_libraries(phigros_render PRIVATE
        phigros_core_lib vendor_stb vendor_miniz vendor_miniaudio
    )
    target_compile_options(phigros_render PRIVATE "SHELL:-s USE_SDL=2")
    target_compile_definitions(phigros_render PRIVATE PHIGROS_SDL2=1 PHIGROS_WASM=1)
    set_target_properties(phigros_render PROPERTIES SUFFIX ".js")
    target_link_options(phigros_render PRIVATE
        "SHELL:-s USE_SDL=2"
        "SHELL:-s USE_WEBGL2=1"
        "SHELL:-s ALLOW_MEMORY_GROWTH=1"
        "SHELL:-s EXPORTED_RUNTIME_METHODS=['FS','callMain','cwrap']"
        "SHELL:-s EXPORTED_FUNCTIONS=['_main','_malloc','_free']"
        "SHELL:-s INVOKE_RUN=0"
        "SHELL:-s FORCE_FILESYSTEM=1"
        "SHELL:--preload-file respack.zip"
        "SHELL:--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html"
    )
else()
    target_include_directories(phigros_render PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(phigros_render PRIVATE
        phigros_core_lib vendor_stb vendor_miniz vendor_miniaudio
        ${SDL2_LIBRARIES}
    )
    target_compile_options(phigros_render PRIVATE ${SDL2_CFLAGS_OTHER})
    target_compile_definitions(phigros_render PRIVATE PHIGROS_SDL2=1)
endif()

if(HAS_BGFX)
    target_link_libraries(phigros_render PRIVATE bgfx bx bimg)
    target_compile_definitions(phigros_render PRIVATE PHIGROS_HAS_BGFX=1)
endif()
