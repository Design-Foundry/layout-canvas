cmake_minimum_required(VERSION 3.25)
project(visualcore VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
)
FetchContent_MakeAvailable(json)

# ---------------------------------------------------------------------------
# vc_core: plain C++. No browser, no GPU. Compiles and tests natively.
# ---------------------------------------------------------------------------
add_library(vc_core STATIC
    src/camera.cpp
    src/color.cpp
    src/demo.cpp
    src/document.cpp
    src/document_tokens.cpp
    src/draw_list.cpp
    src/editor.cpp
    src/fractional_index.cpp
    src/geometry.cpp
    src/id.cpp
    src/layout.cpp
    src/json.cpp
    src/node.cpp
    src/session.cpp
    src/tokens.cpp
    src/viewport_controller.cpp
)
target_include_directories(vc_core PUBLIC include)
target_link_libraries(vc_core PRIVATE nlohmann_json::nlohmann_json)

if(MSVC)  # true for clang-cl too
    target_compile_options(vc_core PRIVATE /W4)
else()
    target_compile_options(vc_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# WASM build: the module the browser loads
# ---------------------------------------------------------------------------
if(EMSCRIPTEN)
    add_executable(visualcore src/main_wasm.cpp src/render/renderer.cpp src/web/input.cpp)
    target_compile_options(visualcore PRIVATE --use-port=emdawnwebgpu)
    target_link_libraries(visualcore PRIVATE vc_core)

    target_link_options(visualcore PRIVATE
        -lembind
        --use-port=emdawnwebgpu
        -sMODULARIZE=1
        -sEXPORT_ES6=1
        -sEXPORT_NAME=createVisualCore
        -sENVIRONMENT=web
        -sALLOW_MEMORY_GROWTH=1
        --emit-tsd=visualcore.d.ts
        $<$<CONFIG:Debug>:-g -sASSERTIONS=2>
        $<$<CONFIG:Release>:-O3>
    )

    # Test page next to the generated visualcore.js / visualcore.wasm
    configure_file(web/index.html ${CMAKE_CURRENT_BINARY_DIR}/index.html COPYONLY)

    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/visualcore.js
        ${CMAKE_CURRENT_BINARY_DIR}/visualcore.wasm
        ${CMAKE_CURRENT_BINARY_DIR}/visualcore.d.ts
        DESTINATION .)

# ---------------------------------------------------------------------------
# Native build: unit tests
# ---------------------------------------------------------------------------
else()
    option(VC_BUILD_TESTS "Build unit tests" ON)
    if(VC_BUILD_TESTS)
        FetchContent_Declare(
            Catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            GIT_TAG        v3.8.1
            GIT_SHALLOW    TRUE
        )
        FetchContent_MakeAvailable(Catch2)

        add_executable(vc_tests
            tests/test_camera.cpp
            tests/test_components.cpp
            tests/test_document.cpp
            tests/test_draw_list.cpp
            tests/test_editor.cpp
            tests/test_json.cpp
            tests/test_layout.cpp
            tests/test_model.cpp
            tests/test_tokens.cpp
            tests/test_viewport_controller.cpp
        )
        target_link_libraries(vc_tests PRIVATE vc_core Catch2::Catch2WithMain)

        enable_testing()
        list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
        include(Catch)
        catch_discover_tests(vc_tests)
    endif()
endif()
