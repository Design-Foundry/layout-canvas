#include "input.hpp"

#include <emscripten/html5.h>

namespace vc::web {
namespace {

constexpr double kLineHeightPx = 16.0;
constexpr double kPageHeightPx = 800.0;

Editor& editorFrom(void* user) {
    return *static_cast<Editor*>(user);
}

Vec2 positionOf(const EmscriptenMouseEvent& e) {
    return {static_cast<double>(e.targetX), static_cast<double>(e.targetY)};
}

Modifiers modifiersOf(const EmscriptenMouseEvent& e) {
    return {e.shiftKey, e.altKey};
}

bool onMouseDown(int, const EmscriptenMouseEvent* e, void* user) {
    editorFrom(user).pointerDown(positionOf(*e), static_cast<PointerButton>(e->button), modifiersOf(*e));
    return true;
}

bool onMouseMove(int, const EmscriptenMouseEvent* e, void* user) {
    editorFrom(user).pointerMove(positionOf(*e), modifiersOf(*e));
    return true;
}

bool onMouseUp(int, const EmscriptenMouseEvent*, void* user) {
    editorFrom(user).pointerUp();
    return true;
}

bool onWheel(int, const EmscriptenWheelEvent* e, void* user) {
    double deltaY = e->deltaY;
    if (e->deltaMode == DOM_DELTA_LINE) {
        deltaY *= kLineHeightPx;
    } else if (e->deltaMode == DOM_DELTA_PAGE) {
        deltaY *= kPageHeightPx;
    }

    editorFrom(user).wheel(positionOf(e->mouse), deltaY);
    return true;
}

bool onKey(int, const EmscriptenKeyboardEvent* e, void* user) {
    editorFrom(user).setModifiers({e->shiftKey, e->altKey});
    return false;
}

} // namespace

void attachInput(const char* canvasSelector, Editor& editor) {
    emscripten_set_mousedown_callback(canvasSelector, &editor, true, onMouseDown);
    emscripten_set_mousemove_callback(canvasSelector, &editor, true, onMouseMove);
    emscripten_set_mouseup_callback(canvasSelector, &editor, true, onMouseUp);
    emscripten_set_mouseleave_callback(canvasSelector, &editor, true, onMouseUp);
    emscripten_set_wheel_callback(canvasSelector, &editor, true, onWheel);
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, &editor, true, onKey);
    emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, &editor, true, onKey);
}

} // namespace vc::web
