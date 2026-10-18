#include "vc/node.hpp"

namespace vc {
namespace {

template <typename T>
void swapIn(std::optional<T>& inverse, const std::optional<T>& next, T& field) {
    if (next) {
        inverse = field;
        field = *next;
    }
}

} // namespace

NodePatch applyPatch(Node& node, const NodePatch& patch) {
    NodePatch inverse;
    swapIn(inverse.name, patch.name, node.name);
    swapIn(inverse.x, patch.x, node.x);
    swapIn(inverse.y, patch.y, node.y);
    swapIn(inverse.w, patch.w, node.w);
    swapIn(inverse.h, patch.h, node.h);
    swapIn(inverse.fill, patch.fill, node.fill);
    swapIn(inverse.fillVisible, patch.fillVisible, node.fillVisible);
    swapIn(inverse.cornerRadius, patch.cornerRadius, node.cornerRadius);
    swapIn(inverse.cornerRadii, patch.cornerRadii, node.cornerRadii);
    swapIn(inverse.stroke, patch.stroke, node.stroke);
    swapIn(inverse.shadows, patch.shadows, node.shadows);
    swapIn(inverse.opacity, patch.opacity, node.opacity);
    swapIn(inverse.visible, patch.visible, node.visible);
    swapIn(inverse.locked, patch.locked, node.locked);
    swapIn(inverse.clipContent, patch.clipContent, node.clipContent);
    swapIn(inverse.layout, patch.layout, node.layout);
    swapIn(inverse.widthSizing, patch.widthSizing, node.widthSizing);
    swapIn(inverse.heightSizing, patch.heightSizing, node.heightSizing);
    swapIn(inverse.limits, patch.limits, node.limits);
    swapIn(inverse.absolute, patch.absolute, node.absolute);
    return inverse;
}

NodePatch fullPatch(const Node& n) {
    NodePatch p;
    p.name = n.name;
    p.x = n.x;
    p.y = n.y;
    p.w = n.w;
    p.h = n.h;
    p.fill = n.fill;
    p.fillVisible = n.fillVisible;
    p.cornerRadius = n.cornerRadius;
    p.cornerRadii = n.cornerRadii;
    p.stroke = n.stroke;
    p.shadows = n.shadows;
    p.opacity = n.opacity;
    p.visible = n.visible;
    p.locked = n.locked;
    p.clipContent = n.clipContent;
    p.layout = n.layout;
    p.widthSizing = n.widthSizing;
    p.heightSizing = n.heightSizing;
    p.limits = n.limits;
    p.absolute = n.absolute;
    return p;
}

bool canHaveChildren(NodeType type) {
    return type == NodeType::Frame;
}

} // namespace vc
