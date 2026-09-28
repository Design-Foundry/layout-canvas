cmake_minimum_required(VERSION 3.25)
project(visualcore VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# vc_core: plain C++. No browser, no GPU. Compiles and tests natively.
# ---------------------------------------------------------------------------
add_library(vc_core STATIC
    src/camera.cpp
)
target_include_directories(vc_core PUBLIC include)

if(MSVC)  # true for clang-cl too
    target_compile_options(vc_core PRIVATE /W4)
else()
    target_compile_options(vc_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# WASM build: the module the browser loads
# ---------------------------------------------------------------------------
if(EMSCRIPTEN)
    add_executable(visualcore src/main_wasm.cpp)
    target_link_libraries(visualcore PRIVATE vc_core)

    target_link_options(visualcore PRIVATE
        -lembind
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
        include(FetchContent)
        FetchContent_Declare(
            Catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            GIT_TAG        v3.8.1
            GIT_SHALLOW    TRUE
        )
        FetchContent_MakeAvailable(Catch2)

        add_executable(vc_tests
            tests/test_camera.cpp
        )
        target_link_libraries(vc_tests PRIVATE vc_core Catch2::Catch2WithMain)

        enable_testing()
        list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
        include(Catch)
        catch_discover_tests(vc_tests)
    endif()
endif()
