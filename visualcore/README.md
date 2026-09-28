# visualcore

C++20 visual core for the canvas editor. `vc_core` is plain C++ (tested natively);
the WASM module wraps it for the browser.

## Native (tests)

VS Code terminals load MSVC + Emscripten automatically. In a plain terminal, run
`. .\tools\devshell.ps1` first.

    cmake --preset native-debug
    cmake --build --preset native-debug
    ctest --preset native-debug

## WASM

    cmake --preset wasm-debug
    cmake --build --preset wasm-debug
    emrun build/wasm-debug/index.html

Open DevTools: the console should show `visualcore: alive` and the camera test.

## Layout

    include/vc/   public headers (math, camera)
    src/          implementation; main_wasm.cpp is the browser entry
    src/render/   (step 2) the only folder allowed to include WebGPU headers
    tests/        Catch2 unit tests, native only
    web/          test page copied next to the build output
