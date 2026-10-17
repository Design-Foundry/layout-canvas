#include "vc/demo.hpp"

#include <random>
#include <string>
#include "vc/fractional_index.hpp"

namespace vc {

void addRandomNodes(Session& session, std::size_t count, const Rect& area, std::uint64_t seed) {
    std::mt19937_64 rng{seed};
    IdGenerator ids{seed};
    std::uniform_real_distribution<double> unit{0.0, 1.0};
    std::uniform_real_distribution<double> size{8.0, 120.0};
    std::uniform_real_distribution<double> hue{0.0, 360.0};

    std::string topOrder;
    for (std::size_t i = 0; i < count;) {
        Node frame;
        frame.id = ids.next();
        frame.type = NodeType::Frame;
        frame.order = topOrder = keyBetween(topOrder, std::nullopt);
        frame.name = "Frame";
        frame.w = size(rng) * 2.5;
        frame.h = size(rng) * 2.5;
        frame.x = area.x + unit(rng) * (area.w - frame.w);
        frame.y = area.y + unit(rng) * (area.h - frame.h);
        frame.fill = Oklch{0.32, 0.01, hue(rng)};
        frame.cornerRadius = 8.0;
        frame.clipContent = unit(rng) < 0.7;
        session.applyRemote(CreateNode{frame});
        ++i;

        std::string childOrder;
        const std::size_t children = std::min<std::size_t>(count - i, 1 + rng() % 8);
        for (std::size_t c = 0; c < children; ++c, ++i) {
            Node child;
            child.id = ids.next();
            child.type = unit(rng) < 0.3 ? NodeType::Ellipse : NodeType::Rect;
            child.parent = frame.id;
            child.order = childOrder = keyBetween(childOrder, std::nullopt);
            child.name = child.type == NodeType::Ellipse ? "Ellipse" : "Rectangle";
            child.w = size(rng) * 0.6;
            child.h = size(rng) * 0.6;
            child.x = unit(rng) * frame.w - child.w * 0.25;
            child.y = unit(rng) * frame.h - child.h * 0.25;
            child.fill = Oklch{0.72, 0.14, hue(rng)};
            child.cornerRadius = unit(rng) < 0.5 ? 0.0 : 6.0;
            session.applyRemote(CreateNode{child});
        }
    }
}

} // namespace vc
