#pragma once

namespace vc {

// World coordinates are doubles on purpose: floats lose precision far from
// the origin and shapes start to jitter.
struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

[[nodiscard]] constexpr Vec2 operator-(Vec2 a, Vec2 b) {
    return {a.x - b.x, a.y - b.y};
}

[[nodiscard]] constexpr Vec2 operator+(Vec2 a, Vec2 b) {
    return {a.x + b.x, a.y + b.y};
}

struct Rect {
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;

    [[nodiscard]] bool intersects(const Rect& o) const {
        return x < o.x + o.w && o.x < x + w && y < o.y + o.h && o.y < y + h;
    }

    [[nodiscard]] bool contains(Vec2 p) const {
        return p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h;
    }
};

} // namespace vc
