#include "vc/geometry.hpp"

namespace vc {
namespace {

void hitChildren(const Document& document, const Id& parent, Vec2 offset, Vec2 world, bool framesOnly,
                 Id& hit) {
    for (const Id& id : document.children(parent)) {
        const Node& node = *document.find(id);
        if (!node.visible) {
            continue;
        }
        const Rect bounds{offset.x + node.x, offset.y + node.y, node.w, node.h};
        const bool inside = bounds.contains(world);
        if (inside && (!framesOnly || node.type == NodeType::Frame)) {
            hit = id;
        }
        if (canHaveChildren(node.type) && (inside || !node.clipContent)) {
            hitChildren(document, id, {bounds.x, bounds.y}, world, framesOnly, hit);
        }
    }
}

} // namespace

std::optional<Rect> worldBounds(const Document& document, const Id& id) {
    const Node* node = document.find(id);
    if (!node) {
        return std::nullopt;
    }
    Rect bounds{node->x, node->y, node->w, node->h};
    for (const Node* p = document.find(node->parent); p; p = document.find(p->parent)) {
        bounds.x += p->x;
        bounds.y += p->y;
    }
    return bounds;
}

Id hitTest(const Document& document, Vec2 world, bool framesOnly) {
    Id hit;
    hitChildren(document, Id{}, {}, world, framesOnly, hit);
    return hit;
}

} // namespace vc
