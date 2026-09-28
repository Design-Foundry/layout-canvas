#include "vc/camera.hpp"
#include <algorithm>

namespace vc {

Vec2 Camera::toScreen(Vec2 w) const {
    return { (w.x - pos_.x) * zoom_, (w.y - pos_.y) * zoom_ };
}

Vec2 Camera::toWorld(Vec2 s) const {
    return { s.x / zoom_ + pos_.x, s.y / zoom_ + pos_.y };
}

Rect Camera::visibleWorld(Vec2 screenSize) const {
    return { pos_.x, pos_.y, screenSize.x / zoom_, screenSize.y / zoom_ };
}

void Camera::pan(Vec2 screenDelta) {
    pos_.x -= screenDelta.x / zoom_;
    pos_.y -= screenDelta.y / zoom_;
}

// Keep the world point under the cursor fixed while zooming.
void Camera::zoomAt(Vec2 screenPoint, double factor) {
    const Vec2 before = toWorld(screenPoint);
    zoom_ = std::clamp(zoom_ * factor, kMinZoom, kMaxZoom);
    const Vec2 after = toWorld(screenPoint);
    pos_.x += before.x - after.x;
    pos_.y += before.y - after.y;
}

} // namespace vc
