#include <cstdio>
#include <emscripten/bind.h>
#include "vc/camera.hpp"

// Milestone step 1: prove C++ runs in the browser and JS can call into it.
// WebGPU arrives in step 2, inside src/render/ (the only place allowed to
// include GPU headers).

int main() {
    std::puts("visualcore: alive");
    return 0;
}

EMSCRIPTEN_BINDINGS(visualcore) {
    using namespace emscripten;

    value_object<vc::Vec2>("Vec2")
        .field("x", &vc::Vec2::x)
        .field("y", &vc::Vec2::y);

    value_object<vc::Rect>("Rect")
        .field("x", &vc::Rect::x)
        .field("y", &vc::Rect::y)
        .field("w", &vc::Rect::w)
        .field("h", &vc::Rect::h);

    class_<vc::Camera>("Camera")
        .constructor<>()
        .function("toScreen", &vc::Camera::toScreen)
        .function("toWorld", &vc::Camera::toWorld)
        .function("visibleWorld", &vc::Camera::visibleWorld)
        .function("pan", &vc::Camera::pan)
        .function("zoomAt", &vc::Camera::zoomAt)
        .function("position", &vc::Camera::position)
        .function("zoom", &vc::Camera::zoom);
}
