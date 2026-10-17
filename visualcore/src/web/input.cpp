#include "input.hpp"

#include <emscripten/html5.h>

namespace vc::web {
namespace {

constexpr double kLineHeightPx = 16.0;
constexpr double kPageHeightPx = 800.0;

ViewportController& controllerFrom(void* user) {
    return *static_cast<ViewportController*>(user);
}

Vec2 positionOf(const EmscriptenMouseEvent& e) {
    return {static_cast<double>(e.targetX), static_cast<double>(e.targetY)};
}

bool onMouseDown(int, const EmscriptenMouseEvent* e, void* user) {
    controllerFrom(user).pointerDown(positionOf(*e));
    return true;
}

bool onMouseMove(int, const EmscriptenMouseEvent* e, void* user) {
    controllerFrom(user).pointerMove(positionOf(*e));
    return true;
}

bool onMouseUp(int, const EmscriptenMouseEvent*, void* user) {
    controllerFrom(user).pointerUp();
    return true;
}

bool onWheel(int, const EmscriptenWheelEvent* e, void* user) {
    double deltaY = e->deltaY;
    if (e->deltaMode == DOM_DELTA_LINE) {
        deltaY *= kLineHeightPx;
    } else if (e->deltaMode == DOM_DELTA_PAGE) {
        deltaY *= kPageHeightPx;
    }

    controllerFrom(user).wheel(positionOf(e->mouse), deltaY);
    return true;
}

} // namespace

void attachInput(const char* canvasSelector, ViewportController& controller) {
    emscripten_set_mousedown_callback(canvasSelector, &controller, true, onMouseDown);
    emscripten_set_mousemove_callback(canvasSelector, &controller, true, onMouseMove);
    emscripten_set_mouseup_callback(canvasSelector, &controller, true, onMouseUp);
    emscripten_set_mouseleave_callback(canvasSelector, &controller, true, onMouseUp);
    emscripten_set_wheel_callback(canvasSelector, &controller, true, onWheel);
}

} // namespace vc::web
