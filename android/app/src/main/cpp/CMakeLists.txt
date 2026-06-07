# Android-side CMakeLists — builds the JNI shared library only.
# Sources live in the cpp/ tree two levels up.
cmake_minimum_required(VERSION 3.22.1)
project(phigros_jni CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Root of the C++ source tree (relative to this file: ../../../../cpp)
set(CPP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../cpp")

include(FetchContent)

# ── nlohmann/json (header-only) ───────────────────────────────────────────────
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(json)

# ── Vendor libraries ──────────────────────────────────────────────────────────
set(VENDOR_DIR "${CPP_ROOT}/vendor")

add_library(vendor_miniz STATIC
    "${VENDOR_DIR}/miniz/miniz.c"
    "${VENDOR_DIR}/miniz/miniz_tdef.c"
    "${VENDOR_DIR}/miniz/miniz_tinfl.c"
    "${VENDOR_DIR}/miniz/miniz_zip.c"
)
target_include_directories(vendor_miniz PUBLIC "${VENDOR_DIR}/miniz")

add_library(vendor_stb STATIC "${CPP_ROOT}/src/vendor/vendor_impl.cpp")
target_include_directories(vendor_stb PUBLIC "${VENDOR_DIR}/stb")

add_library(vendor_miniaudio STATIC "${CPP_ROOT}/src/vendor/audio_impl.cpp")
target_include_directories(vendor_miniaudio PUBLIC
    "${VENDOR_DIR}/miniaudio"
    "${VENDOR_DIR}/stb")
target_link_libraries(vendor_miniaudio PUBLIC dl)

# ── Core library ──────────────────────────────────────────────────────────────
add_library(phigros_core_lib STATIC
    "${CPP_ROOT}/src/chart/official.cpp"
    "${CPP_ROOT}/src/chart/rpe.cpp"
    "${CPP_ROOT}/src/chart/pec.cpp"
    "${CPP_ROOT}/src/chart/compiler.cpp"
    "${CPP_ROOT}/src/chart/phbc_io.cpp"
    "${CPP_ROOT}/src/chart/phbc_compress.cpp"
    "${CPP_ROOT}/src/chart/phbc_crypto.cpp"
    "${CPP_ROOT}/src/chart/chart_loader.cpp"
)
target_include_directories(phigros_core_lib PUBLIC "${CPP_ROOT}/include")
target_include_directories(phigros_core_lib PRIVATE "${VENDOR_DIR}/miniz")
target_link_libraries(phigros_core_lib PUBLIC nlohmann_json::nlohmann_json)
target_link_libraries(phigros_core_lib PRIVATE vendor_miniz)

# ── Python-api lib (bridge depends on it) ─────────────────────────────────────
add_library(phigros_python_api_lib STATIC
    "${CPP_ROOT}/src/api/python_api.cpp"
)
target_include_directories(phigros_python_api_lib PUBLIC "${CPP_ROOT}/include")
target_link_libraries(phigros_python_api_lib PUBLIC phigros_core_lib)

# ── Mobile bridge ─────────────────────────────────────────────────────────────
add_library(phigros_mobile_bridge STATIC
    "${CPP_ROOT}/src/api/mobile_bridge.cpp"
)
target_include_directories(phigros_mobile_bridge PUBLIC "${CPP_ROOT}/include")
target_link_libraries(phigros_mobile_bridge PUBLIC phigros_python_api_lib)

# ── JNI shared library (loaded by Kotlin via System.loadLibrary) ──────────────
add_library(phigros_jni SHARED
    "${CPP_ROOT}/src/api/mobile_bridge_jni.cpp"
)
target_include_directories(phigros_jni PRIVATE "${CPP_ROOT}/include")
target_link_libraries(phigros_jni PRIVATE
    phigros_mobile_bridge
    android
    log
)
