#include "vc/scene.hpp"

#include <random>

namespace vc {

void Scene::visible(const Rect& world, std::vector<Shape>& out) const {
    out.clear();
    for (const Shape& shape : shapes_) {
        if (shape.bounds.intersects(world)) {
            out.push_back(shape);
        }
    }
}

void addRandomShapes(Scene& scene, std::size_t count, const Rect& area, std::uint32_t seed) {
    std::mt19937 rng{seed};
    std::uniform_real_distribution<double> size{8.0, 120.0};
    std::uniform_real_distribution<double> unit{0.0, 1.0};
    std::uniform_real_distribution<float> channel{0.25f, 0.9f};

    for (std::size_t i = 0; i < count; ++i) {
        const double w = size(rng);
        const double h = size(rng);
        scene.add({
            {area.x + unit(rng) * (area.w - w), area.y + unit(rng) * (area.h - h), w, h},
            {channel(rng), channel(rng), channel(rng), 1.0f},
        });
    }
}

} // namespace vc
