cmake_minimum_required(VERSION 3.16)
project(phigros_core CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(FetchContent)

option(BUILD_PYTHON_BINDINGS "Build Python bindings" OFF)
option(BUILD_RENDER_APP "Build native render executable" ON)

# ── Version ───────────────────────────────────────────────────────────────────
set(PHIGROS_VERSION "1.0.0-dev")
add_compile_definitions(PHIGROS_VERSION="${PHIGROS_VERSION}")

# ── Mobile platform configuration ─────────────────────────────────────────────
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/mobile.cmake" OPTIONAL)

# --- nlohmann/json: prefer system package, fallback to FetchContent ---
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    FetchContent_Declare(
        json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        v3.11.3
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(json)
endif()

# --- SDL3 via FetchContent ---
option(USE_SDL3 "Build with SDL3 instead of SDL2" ON)
option(USE_BGFX "Build with bgfx GPU renderer" ON)
option(USE_LIBAV "Use libavcodec/libavformat for video encoding (no FFmpeg subprocess)" ON)
option(USE_LZMA "Build with LZMA compression support for PHBC v2" OFF)
option(USE_ENCRYPTION "Build with OpenSSL encryption support for PHBC v2" ON)
option(USE_SANITIZERS "Enable AddressSanitizer + UBSan (Debug builds only)" OFF)

# --- libav (FFmpeg dev libs) ---
if(BUILD_RENDER_APP AND USE_LIBAV AND NOT EMSCRIPTEN)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBAV QUIET libavcodec libavformat libavutil libswscale libswresample)
    endif()
    if(LIBAV_FOUND)
        set(HAS_LIBAV TRUE)
        message(STATUS "[libav] Found: avcodec ${libavcodec_VERSION}")
    else()
        set(HAS_LIBAV FALSE)
        message(STATUS "[libav] Not found, falling back to FFmpeg subprocess encoder")
    endif()
else()
    set(HAS_LIBAV FALSE)
endif()

# --- LZMA (optional, for PHBC v2 LZMA compression) ---
if(USE_LZMA AND NOT EMSCRIPTEN)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBLZMA QUIET liblzma)
    endif()
    if(LIBLZMA_FOUND)
        set(HAS_LZMA TRUE)
        message(STATUS "[lzma] Found: liblzma ${LIBLZMA_VERSION}")
    else()
        set(HAS_LZMA FALSE)
        message(STATUS "[lzma] Not found, PHBC LZMA compression disabled")
    endif()
else()
    set(HAS_LZMA FALSE)
endif()

# --- OpenSSL (optional, for PHBC v2 encryption) ---
if(USE_ENCRYPTION AND NOT EMSCRIPTEN)
    find_package(OpenSSL QUIET)
    if(OpenSSL_FOUND)
        set(HAS_OPENSSL TRUE)
        message(STATUS "[openssl] Found: OpenSSL ${OPENSSL_VERSION}")
    else()
        set(HAS_OPENSSL FALSE)
        message(STATUS "[openssl] Not found, PHBC encryption limited to XOR only")
    endif()
else()
    set(HAS_OPENSSL FALSE)
endif()

if(BUILD_RENDER_APP)
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
endif()

# --- bgfx via FetchContent ---
if(BUILD_RENDER_APP AND USE_BGFX AND NOT EMSCRIPTEN)
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

# --- Vendor libraries (stb, miniz, miniaudio) — defined early for core_lib ---
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
target_include_directories(vendor_miniaudio PUBLIC "${VENDOR_DIR}/miniaudio" "${VENDOR_DIR}/stb")
if(NOT EMSCRIPTEN)
    target_link_libraries(vendor_miniaudio PUBLIC dl pthread m)
endif()

# --- Core library (math + types + parsers) ---
add_library(phigros_core_lib STATIC
    src/chart/official.cpp
    src/chart/rpe.cpp
    src/chart/pec.cpp
    src/chart/compiler.cpp
    src/chart/phbc_io.cpp
    src/chart/phbc_compress.cpp
    src/chart/phbc_crypto.cpp
    src/chart/chart_loader.cpp
)
target_include_directories(phigros_core_lib PUBLIC include)
target_include_directories(phigros_core_lib PRIVATE "${VENDOR_DIR}/miniz")
target_link_libraries(phigros_core_lib PUBLIC nlohmann_json::nlohmann_json)
target_link_libraries(phigros_core_lib PRIVATE vendor_miniz)

if(HAS_LZMA)
    target_include_directories(phigros_core_lib PRIVATE ${LIBLZMA_INCLUDE_DIRS})
    if(LIBLZMA_LIBRARY_DIRS)
        target_link_directories(phigros_core_lib PRIVATE ${LIBLZMA_LIBRARY_DIRS})
    endif()
    target_link_libraries(phigros_core_lib PRIVATE ${LIBLZMA_LIBRARIES})
    target_compile_definitions(phigros_core_lib PRIVATE PHIGROS_HAS_LZMA=1)
endif()

if(HAS_OPENSSL)
    target_link_libraries(phigros_core_lib PRIVATE OpenSSL::Crypto)
    target_compile_definitions(phigros_core_lib PRIVATE PHIGROS_HAS_OPENSSL=1)
endif()

# --- Sanitizer support (development only, not for release) ---
if(USE_SANITIZERS AND NOT EMSCRIPTEN)
    set(SANITIZER_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer)
    message(STATUS "[sanitizers] Enabled: ASan + UBSan")
    add_compile_options(${SANITIZER_FLAGS})
    add_link_options(${SANITIZER_FLAGS})
endif()

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
    if(HAS_OPENSSL)
        target_compile_definitions(test_engine PRIVATE PHIGROS_HAS_OPENSSL=1)
    endif()
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_parser.cpp")
    add_executable(test_parser tests/test_parser.cpp)
    target_link_libraries(test_parser PRIVATE phigros_core_lib vendor_miniz)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/bench.cpp")
    add_executable(bench tests/bench.cpp)
    target_link_libraries(bench PRIVATE phigros_core_lib vendor_miniz)
    if(UNIX AND NOT APPLE)
        target_link_libraries(bench PRIVATE stdc++fs)
    endif()
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_zip_extract.cpp")
    add_executable(test_zip_extract tests/test_zip_extract.cpp)
    target_link_libraries(test_zip_extract PRIVATE phigros_core_lib vendor_miniz)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_logger.cpp")
    add_executable(test_logger tests/test_logger.cpp)
    target_link_libraries(test_logger PRIVATE phigros_core_lib)
endif()

# --- Chart scanner tool ---
add_executable(chart_scanner src/chart/chart_scanner.cpp)
target_link_libraries(chart_scanner PRIVATE phigros_core_lib)

add_library(phigros_python_api_lib STATIC
    src/api/python_api.cpp
)
target_include_directories(phigros_python_api_lib PUBLIC include)
target_link_libraries(phigros_python_api_lib PUBLIC phigros_core_lib)

# --- Convenience target: run all test executables ---
# Usage: cmake --build build --target run-tests
set(TEST_TARGETS "")
foreach(tgt test_easing test_engine test_parser test_logger test_zip_extract verify_chart)
    if(TARGET ${tgt})
        list(APPEND TEST_TARGETS ${tgt})
    endif()
endforeach()
if(TEST_TARGETS)
    add_custom_target(run-tests
        COMMENT "Running all test executables"
        DEPENDS ${TEST_TARGETS}
    )
    foreach(tgt ${TEST_TARGETS})
        add_custom_command(TARGET run-tests POST_BUILD
            COMMAND $<TARGET_FILE:${tgt}>
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
            COMMENT "Running ${tgt}..."
        )
    endforeach()
endif()

if(BUILD_RENDER_APP)
    # --- Render executable ---
    add_executable(phigros_render src/app/main.cpp)
    target_include_directories(phigros_render PRIVATE
        "${VENDOR_DIR}/stb"
        "${VENDOR_DIR}/miniz"
        "${VENDOR_DIR}/miniaudio"
    )

    # Platform detection macro (used by platform.hpp / sdl_compat.hpp)
    if(ANDROID)
        target_compile_definitions(phigros_render PRIVATE PHIGROS_MOBILE=1 PHIGROS_ANDROID=1 PHIGROS_TOUCH_INPUT=1)
    elseif(IOS)
        target_compile_definitions(phigros_render PRIVATE PHIGROS_MOBILE=1 PHIGROS_IOS=1 PHIGROS_TOUCH_INPUT=1)
    endif()

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
        # SDL2 port is built into Emscripten SDK
        target_compile_options(phigros_render PRIVATE "SHELL:-s USE_SDL=2")
        target_compile_definitions(phigros_render PRIVATE PHIGROS_SDL2=1 PHIGROS_WASM=1 PHIGROS_TOUCH_INPUT=1)
        set_target_properties(phigros_render PROPERTIES SUFFIX ".js")
        target_link_options(phigros_render PRIVATE
            "SHELL:-s USE_SDL=2"
            "SHELL:-s USE_WEBGL2=1"
            "SHELL:-s ALLOW_MEMORY_GROWTH=1"
            "SHELL:-s MAXIMUM_MEMORY=512mb"
            "SHELL:-s EXPORTED_RUNTIME_METHODS=[\"FS\",\"callMain\",\"cwrap\",\"UTF8ToString\"]"
            "SHELL:-s EXPORTED_FUNCTIONS=[\"_main\",\"_malloc\",\"_free\"]"
            "SHELL:-s INVOKE_RUN=0"
            "SHELL:-s FORCE_FILESYSTEM=1"
            "SHELL:-s ENVIRONMENT=web"
            "SHELL:-s ASSERTIONS=0"
            "SHELL:-O3"
            "SHELL:--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html"
        )
        # Only preload respack.zip if it exists at configure time
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../respack.zip")
            target_link_options(phigros_render PRIVATE
                "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/../respack.zip@/respack.zip"
            )
        endif()
    else()
        target_include_directories(phigros_render PRIVATE ${SDL2_INCLUDE_DIRS})
        if(SDL2_LIBRARY_DIRS)
            target_link_directories(phigros_render PRIVATE ${SDL2_LIBRARY_DIRS})
        endif()
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

    if(HAS_LIBAV)
        target_include_directories(phigros_render PRIVATE ${LIBAV_INCLUDE_DIRS})
        if(LIBAV_LIBRARY_DIRS)
            target_link_directories(phigros_render PRIVATE ${LIBAV_LIBRARY_DIRS})
        endif()
        target_link_libraries(phigros_render PRIVATE ${LIBAV_LIBRARIES})
        target_compile_definitions(phigros_render PRIVATE PHIGROS_HAS_LIBAV=1)
    endif()

    # ── iOS app bundle (only when targeting iOS via Xcode) ───────────────────
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        add_subdirectory(ios)
    endif()
endif()

if(BUILD_PYTHON_BINDINGS)
    find_package(Python COMPONENTS Interpreter Development.Module REQUIRED)
    find_package(pybind11 CONFIG QUIET)
    if(NOT pybind11_FOUND)
        FetchContent_Declare(
            pybind11
            GIT_REPOSITORY https://github.com/pybind/pybind11.git
            GIT_TAG v2.13.6
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(pybind11)
    endif()

    pybind11_add_module(_core src/python/module.cpp)
    target_include_directories(_core PRIVATE include)
    target_link_libraries(_core PRIVATE phigros_python_api_lib)
    set_target_properties(_core PROPERTIES
        OUTPUT_NAME "_core"
    )
    install(TARGETS _core
        LIBRARY DESTINATION phigros_cpp
        RUNTIME DESTINATION phigros_cpp
    )
endif()
