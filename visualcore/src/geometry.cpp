#include "vc/geometry.hpp"

namespace vc {
namespace {

void hitChildren(const Document& document, const Id& parent, Vec2 offset, Vec2 world, bool framesOnly,
                 const Id& mode, Id& hit) {
    for (const Id& id : document.viewChildren(parent)) {
        const Node& node = *document.view(id);
        if (!node.visible) {
            continue;
        }
        const Rect box = document.box(id, mode);
        const Rect bounds{offset.x + box.x, offset.y + box.y, box.w, box.h};
        const bool inside = bounds.contains(world);
        if (inside && !node.locked && (!framesOnly || node.type == NodeType::Frame || node.type == NodeType::Component)) {
            hit = id;
        }
        if (isContainer(node.type) && (inside || !node.clipContent)) {
            hitChildren(document, id, {bounds.x, bounds.y}, world, framesOnly, mode, hit);
        }
    }
}

void frameChildren(const Document& document, const Id& parent, Vec2 offset, Vec2 world, const Id& mode,
                   const std::unordered_set<Id>& excluded, Id& hit) {
    for (const Id& id : document.viewChildren(parent)) {
        const Node& node = *document.view(id);
        const bool frame = node.type == NodeType::Frame || node.type == NodeType::Component;
        if (!node.visible || excluded.contains(id) || !frame || document.ownerOf(id) != id) {
            continue;
        }
        const Rect box = document.box(id, mode);
        const Rect bounds{offset.x + box.x, offset.y + box.y, box.w, box.h};
        if (bounds.contains(world)) {
            if (!node.locked) hit = id;
            frameChildren(document, id, {bounds.x, bounds.y}, world, mode, excluded, hit);
        }
    }
}

} // namespace

Id frameAt(const Document& document, Vec2 world, const Id& mode, const std::unordered_set<Id>& excluded) {
    Id hit;
    frameChildren(document, Id{}, {}, world, mode, excluded, hit);
    return hit;
}

std::optional<Rect> worldBounds(const Document& document, const Id& id, const Id& mode) {
    if (!document.view(id)) {
        return std::nullopt;
    }
    Rect bounds = document.box(id, mode);
    for (const Node* p = document.view(document.view(id)->parent); p; p = document.view(p->parent)) {
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
