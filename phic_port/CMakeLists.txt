cmake_minimum_required(VERSION 3.20)
project(phic_port LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PHIC_BUILD_TESTS "Build unit tests" ON)
option(PHIC_BUILD_DESKTOP_APP "Build desktop app" ON)
option(PHIC_BUILD_WEB "Build Emscripten WebAssembly artifacts" OFF)

add_library(phic_core
    core/src/parser.cpp
    core/src/engine.cpp
    core/src/mods.cpp
    core/src/mod_config_json.cpp
    core/src/video_recorder.cpp
)

target_include_directories(phic_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/core/include
)

find_package(nlohmann_json CONFIG QUIET)
if(nlohmann_json_FOUND)
    target_link_libraries(phic_core PUBLIC nlohmann_json::nlohmann_json)
else()
    target_include_directories(phic_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
endif()

add_library(phic_c_api
    core_c_api/src/phic_c_api.cpp
)

target_include_directories(phic_c_api
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/core_c_api/include
)

target_link_libraries(phic_c_api PUBLIC phic_core)

if(PHIC_BUILD_DESKTOP_APP)
    add_executable(phic_cli desktop_app/src/main.cpp)
    target_link_libraries(phic_cli PRIVATE phic_core)
endif()

if(PHIC_BUILD_TESTS)
    enable_testing()
    add_executable(phic_core_tests tests/test_engine.cpp)
    target_link_libraries(phic_core_tests PRIVATE phic_core)
    add_test(NAME phic_core_tests COMMAND phic_core_tests)

    add_executable(phic_c_api_tests tests/test_c_api.cpp)
    target_link_libraries(phic_c_api_tests PRIVATE phic_c_api)
    add_test(NAME phic_c_api_tests COMMAND phic_c_api_tests)

    add_executable(phic_oracle_dump tests/oracle_dump.cpp)
    target_link_libraries(phic_oracle_dump PRIVATE phic_core)

    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../phic_renderer")
        add_test(
            NAME phic_parity_oracle
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/oracle_compare.py $<TARGET_FILE:phic_oracle_dump>
        )
    endif()
endif()

if(PHIC_BUILD_WEB)
    if(NOT EMSCRIPTEN)
        message(WARNING "PHIC_BUILD_WEB is ON but EMSCRIPTEN toolchain is not active. Skipping web target.")
    else()
        set(PHIC_WEB_WASM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../phic_web/wasm")
        file(MAKE_DIRECTORY "${PHIC_WEB_WASM_DIR}")

        add_executable(phic_web wasm/entry.cpp)
        target_link_libraries(phic_web PRIVATE phic_c_api)

        target_link_options(phic_web PRIVATE
            "-sMODULARIZE=1"
            "-sEXPORT_ES6=1"
            "-sALLOW_MEMORY_GROWTH=1"
            "-sENVIRONMENT=web,worker"
            "-sEXPORTED_FUNCTIONS=['_malloc','_free','_phic_abi_version','_phic_engine_create','_phic_engine_destroy','_phic_engine_load_chart','_phic_engine_step','_phic_engine_step_ex','_phic_engine_step_auto','_phic_engine_step_auto_ex','_phic_engine_step_v2','_phic_engine_step_auto_v2','_phic_engine_seek','_phic_engine_reset','_phic_engine_last_error']"
            "-sEXPORTED_RUNTIME_METHODS=['cwrap','ccall','UTF8ToString','stringToUTF8','lengthBytesUTF8']"
        )

        set_target_properties(phic_web PROPERTIES
            OUTPUT_NAME "phic_web"
            RUNTIME_OUTPUT_DIRECTORY "${PHIC_WEB_WASM_DIR}"
        )
    endif()
endif()
