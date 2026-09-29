#include "vc/viewport_controller.hpp"
#include <cmath>

namespace vc {
ViewportController::ViewportController(Camera& camera) : camera_(camera) {}

void ViewportController::pointerDown(Vec2 screen) {
    panning_ = true;
    last_ = screen;
}

void ViewportController::pointerMove(Vec2 screen) {
    if (panning_) {
        const Vec2 delta = screen - last_;
        last_ = screen;
        camera_.pan(delta);
    }
}

void ViewportController::pointerUp() {
    panning_ = false;
}

void ViewportController::wheel(Vec2 screen, double deltaY) {
    const double factor = std::exp(-deltaY * kWheelZoomSpeed);
    camera_.zoomAt(screen, factor);
}
}  // namespace vc
