#pragma once
#include "vc/camera.hpp"

namespace vc {
class ViewportController {
public:
    explicit ViewportController(Camera& camera);

    void pointerDown(Vec2 screen);
    void pointerMove(Vec2 screen);
    void pointerUp();
    void wheel(Vec2 screen, double deltaY);

    [[nodiscard]] bool isPanning() const { return panning_; }

    static constexpr double kWheelZoomSpeed = 0.0015;

private:
    Camera& camera_;
    bool panning_ = false;
    Vec2 last_{};
};
}  // namespace vc
