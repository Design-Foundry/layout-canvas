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

FetchContent_Declare(
    harfbuzz
    URL https://github.com/harfbuzz/harfbuzz/releases/download/15.0.0/harfbuzz-15.0.0.tar.xz
    URL_HASH SHA256=968536e9abd782f07f7954044c0c4020199fcdd39b41d221ec42d2a3be4a44df
    SOURCE_SUBDIR amalgamated-only
)
FetchContent_MakeAvailable(harfbuzz)
add_library(vc_harfbuzz STATIC ${harfbuzz_SOURCE_DIR}/src/harfbuzz.cc)
target_include_directories(vc_harfbuzz PUBLIC ${harfbuzz_SOURCE_DIR}/src)
target_compile_definitions(vc_harfbuzz PRIVATE HB_NO_AAT HB_NO_PAINT HB_NO_COLOR HB_NO_LEGACY HB_NO_BUFFER_SERIALIZE
    HB_NO_OT_FONT_GLYPH_NAMES HB_NO_FACE_COLLECT_UNICODES HB_NO_MATH HB_NO_META HB_NO_BASE
    HB_NO_NAME HB_NO_ERRNO HB_NO_GETENV HB_NO_OPEN HB_NO_SETLOCALE HB_NO_STYLE $<$<BOOL:${EMSCRIPTEN}>:HB_NO_MT>)
set_target_properties(vc_harfbuzz PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(EMSCRIPTEN)
    target_compile_options(vc_harfbuzz PRIVATE -fno-exceptions -fno-rtti)
endif()

# The exact trusted font bytes are shared by native layout and the WASM glyph atlas.
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
foreach(FACE IN ITEMS "inter;Inter" "serif;SourceSerif4" "mono;JetBrainsMono")
    list(GET FACE 0 FACE_NAME)
    list(GET FACE 1 FACE_FILE)
    string(TOUPPER "${FACE_NAME}" FACE_UPPER)
    set(FACE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/${FACE_FILE}.ttf")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${FACE_PATH}")
    file(READ "${FACE_PATH}" FACE_HEX HEX)
    string(REGEX REPLACE "(..)" "0x\\1," FACE_ARRAY "${FACE_HEX}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/vc_${FACE_NAME}_font.inc" "${FACE_ARRAY}")
    file(SHA256 "${FACE_PATH}" VC_${FACE_UPPER}_SHA256)
endforeach()

# ---------------------------------------------------------------------------
# vc_core: plain C++. No browser, no GPU. Compiles and tests natively.
# ---------------------------------------------------------------------------
add_library(vc_core STATIC
    src/camera.cpp
    src/color.cpp
    src/demo.cpp
    src/diagnostics.cpp
    src/document.cpp
    src/document_tokens.cpp
    src/draw_list.cpp
    src/editor.cpp
    src/font.cpp
    src/fonts.cpp
    src/fractional_index.cpp
    src/geometry.cpp
    src/icon.cpp
    src/id.cpp
    src/image_export.cpp
    src/layout.cpp
    src/json.cpp
    src/node.cpp
    src/session.cpp
    src/sha256.cpp
    src/tokens.cpp
    src/text.cpp
    src/raster.cpp
    src/viewport_controller.cpp
)
target_include_directories(vc_core PUBLIC include)
target_include_directories(vc_core PRIVATE third_party "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(vc_core PRIVATE nlohmann_json::nlohmann_json vc_harfbuzz)
target_compile_definitions(vc_core PRIVATE VC_INTER_SHA256="${VC_INTER_SHA256}" VC_SERIF_SHA256="${VC_SERIF_SHA256}" VC_MONO_SHA256="${VC_MONO_SHA256}")

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
        -sSTACK_SIZE=4194304
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
# Native build: the shared library the server loads, and unit tests
# ---------------------------------------------------------------------------
else()
    set_target_properties(vc_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
    add_library(vc_native SHARED src/native.cpp)
    target_link_libraries(vc_native PRIVATE vc_core)
    target_compile_definitions(vc_native PRIVATE VC_NATIVE_BUILD)
    set_target_properties(vc_native PROPERTIES C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden)
    if(WIN32)
        # Match the server's vc_native.dll contract with both MSVC and MinGW.
        set_target_properties(vc_native PROPERTIES PREFIX "")
    endif()

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
            tests/test_clone.cpp
            tests/test_notes.cpp
            tests/test_components.cpp
            tests/test_diagnostics.cpp
            tests/test_diagnostics_json.cpp
            tests/test_diagnostics_tokens.cpp
            tests/test_document.cpp
            tests/test_draw_list.cpp
            tests/test_editor.cpp
            tests/test_json.cpp
            tests/test_layout.cpp
            tests/test_model.cpp
            tests/test_native.cpp
            tests/test_tokens.cpp
            tests/test_text.cpp
            tests/test_font_registry.cpp
            tests/test_font_model.cpp
            tests/test_font_stacks.cpp
            tests/test_font_families.cpp
            tests/test_font_availability.cpp
            tests/test_icons.cpp
            tests/test_line.cpp
            tests/test_path.cpp
            tests/test_prototype_model.cpp
            tests/test_prototype_editor.cpp
            tests/test_image.cpp
            tests/test_image_editor.cpp
            tests/test_image_export.cpp
            tests/test_path_editor.cpp
            tests/test_viewport_controller.cpp
        )
        target_link_libraries(vc_tests PRIVATE vc_core vc_native Catch2::Catch2WithMain)
        target_compile_definitions(vc_tests PRIVATE VC_FONT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts")

        enable_testing()
        list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
        include(Catch)
        catch_discover_tests(vc_tests)
    endif()
endif()
