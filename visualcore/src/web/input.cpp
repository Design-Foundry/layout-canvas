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
    return {e.shiftKey, e.altKey, e.ctrlKey || e.metaKey};
}

bool onMouseDown(int, const EmscriptenMouseEvent* e, void* user) {
    editorFrom(user).pointerDown(positionOf(*e), static_cast<PointerButton>(e->button), modifiersOf(*e));
    return true;
}

bool onMouseMove(int, const EmscriptenMouseEvent* e, void* user) {
    editorFrom(user).pointerMove(positionOf(*e), modifiersOf(*e));
    return true;
}

bool onDoubleClick(int, const EmscriptenMouseEvent* e, void* user) {
    editorFrom(user).doubleClick(positionOf(*e));
    return true;
}

bool onMouseUp(int event, const EmscriptenMouseEvent* e, void* user) {
    auto& editor=editorFrom(user);
    if(editor.presenting()) {
        if(event==EMSCRIPTEN_EVENT_MOUSELEAVE) {editor.pointerCancel();return true;}
        editor.pointerUpAt(positionOf(*e));return true;
    }
    editor.pointerUp();
    return true;
}

bool onWheel(int, const EmscriptenWheelEvent* e, void* user) {
    double deltaY = e->deltaY;
    double deltaX = e->deltaX;
    if (e->deltaMode == DOM_DELTA_LINE) {
        deltaY *= kLineHeightPx;
        deltaX *= kLineHeightPx;
    } else if (e->deltaMode == DOM_DELTA_PAGE) {
        deltaY *= kPageHeightPx;
        deltaX *= kPageHeightPx;
    }

    // Preserve horizontal trackpad input; Shift-wheel also works with a mouse.
    if(editorFrom(user).presenting() && e->mouse.shiftKey && deltaX==0) {
        deltaX=deltaY;deltaY=0;
    }
    editorFrom(user).wheel(positionOf(e->mouse), deltaY, deltaX);
    return true;
}

bool onTouch(int event,const EmscriptenTouchEvent* e,void* user) {
    auto& editor=editorFrom(user);
    if(!editor.presenting()) return false;
    if(event==EMSCRIPTEN_EVENT_TOUCHCANCEL) {editor.pointerCancel();return true;}
    if(event==EMSCRIPTEN_EVENT_TOUCHEND) {
        editor.prototypeTouchEnd();return true;
    }
    if(e->numTouches!=1) {editor.pointerCancel();return true;}
    const Vec2 point{double(e->touches[0].targetX),double(e->touches[0].targetY)};
    if(event==EMSCRIPTEN_EVENT_TOUCHSTART) editor.prototypeTouchStart(point);
    else editor.prototypeTouchMove(point);
    return true;
}

bool onKey(int, const EmscriptenKeyboardEvent* e, void* user) {
    editorFrom(user).setModifiers({e->shiftKey, e->altKey, e->ctrlKey || e->metaKey});
    return false;
}

} // namespace

void attachInput(const char* canvasSelector, Editor& editor) {
    emscripten_set_mousedown_callback(canvasSelector, &editor, true, onMouseDown);
    emscripten_set_mousemove_callback(canvasSelector, &editor, true, onMouseMove);
    emscripten_set_mouseup_callback(canvasSelector, &editor, true, onMouseUp);
    emscripten_set_mouseleave_callback(canvasSelector, &editor, true, onMouseUp);
    emscripten_set_dblclick_callback(canvasSelector, &editor, true, onDoubleClick);
    emscripten_set_wheel_callback(canvasSelector, &editor, true, onWheel);
    emscripten_set_touchstart_callback(canvasSelector, &editor, true, onTouch);
    emscripten_set_touchmove_callback(canvasSelector, &editor, true, onTouch);
    emscripten_set_touchend_callback(canvasSelector, &editor, true, onTouch);
    emscripten_set_touchcancel_callback(canvasSelector, &editor, true, onTouch);
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, &editor, true, onKey);
    emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, &editor, true, onKey);
}

} // namespace vc::web
