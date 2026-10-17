#pragma once
#include <cstdint>
#include <vector>
#include "vc/math.hpp"

namespace vc {

struct Color {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

struct Shape {
    Rect bounds;
    Color fill;
};

/// Flat list of shapes in world space. Stands in for the document model until phase 2.
class Scene {
public:
    void add(Shape shape) { shapes_.push_back(shape); }
    void clear() { shapes_.clear(); }

    [[nodiscard]] const std::vector<Shape>& shapes() const { return shapes_; }

    /// Replaces `out` with the shapes that intersect `world`, in draw order.
    void visible(const Rect& world, std::vector<Shape>& out) const;

private:
    std::vector<Shape> shapes_;
};

/// Fills `scene` with `count` random shapes inside `area`. Same seed, same scene.
void addRandomShapes(Scene& scene, std::size_t count, const Rect& area, std::uint32_t seed);

} // namespace vc
