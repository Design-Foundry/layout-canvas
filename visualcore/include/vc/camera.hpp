#pragma once
#include "vc/math.hpp"

namespace vc {

// The only thing that knows about both world and screen space.
class Camera {
public:
    [[nodiscard]] Vec2 toScreen(Vec2 world) const;
    [[nodiscard]] Vec2 toWorld(Vec2 screen) const;
    [[nodiscard]] Rect visibleWorld(Vec2 screenSize) const;

    void pan(Vec2 screenDelta);
    void zoomAt(Vec2 screenPoint, double factor);

    [[nodiscard]] Vec2 position() const { return pos_; }
    [[nodiscard]] double zoom() const { return zoom_; }

    static constexpr double kMinZoom = 0.01;
    static constexpr double kMaxZoom = 256.0;

private:
    Vec2 pos_{};        // world point at the top-left of the screen
    double zoom_ = 1.0;
};

} // namespace vc
