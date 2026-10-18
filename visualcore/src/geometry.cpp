#include "vc/geometry.hpp"

namespace vc {
namespace {

void hitChildren(const Document& document, const Id& parent, Vec2 offset, Vec2 world, bool framesOnly,
                 const Id& mode, Id& hit) {
    for (const Id& id : document.children(parent)) {
        const Node& node = *document.find(id);
        if (!node.visible) {
            continue;
        }
        const Rect box = document.box(id, mode);
        const Rect bounds{offset.x + box.x, offset.y + box.y, box.w, box.h};
        const bool inside = bounds.contains(world);
        if (inside && !node.locked && (!framesOnly || node.type == NodeType::Frame)) {
            hit = id;
        }
        if (canHaveChildren(node.type) && (inside || !node.clipContent)) {
            hitChildren(document, id, {bounds.x, bounds.y}, world, framesOnly, mode, hit);
        }
    }
}

} // namespace

std::optional<Rect> worldBounds(const Document& document, const Id& id, const Id& mode) {
    if (!document.find(id)) {
        return std::nullopt;
    }
    Rect bounds = document.box(id, mode);
    for (const Node* p = document.find(document.find(id)->parent); p; p = document.find(p->parent)) {
        const Rect parent = document.box(p->id, mode);
        bounds.x += parent.x;
        bounds.y += parent.y;
    }
    return bounds;
}

Id hitTest(const Document& document, Vec2 world, bool framesOnly, const Id& mode) {
    Id hit;
    hitChildren(document, Id{}, {}, world, framesOnly, mode, hit);
    return hit;
}

} // namespace vc
