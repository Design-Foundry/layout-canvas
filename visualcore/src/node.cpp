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
    swapIn(inverse.type, patch.type, node.type);
    swapIn(inverse.component, patch.component, node.component);
    return inverse;
}

NodePatch mergePatch(const NodePatch& base, const NodePatch& top) {
    NodePatch out = base;
    const auto take = [](auto& field, const auto& value) {
        if (value) field = value;
    };
    take(out.name, top.name);
    take(out.x, top.x);
    take(out.y, top.y);
    take(out.w, top.w);
    take(out.h, top.h);
    take(out.fill, top.fill);
    take(out.fillVisible, top.fillVisible);
    take(out.cornerRadius, top.cornerRadius);
    take(out.cornerRadii, top.cornerRadii);
    take(out.stroke, top.stroke);
    take(out.shadows, top.shadows);
    take(out.opacity, top.opacity);
    take(out.visible, top.visible);
    take(out.locked, top.locked);
    take(out.clipContent, top.clipContent);
    take(out.layout, top.layout);
    take(out.widthSizing, top.widthSizing);
    take(out.heightSizing, top.heightSizing);
    take(out.limits, top.limits);
    take(out.absolute, top.absolute);
    take(out.type, top.type);
    take(out.component, top.component);
    return out;
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
    p.type = n.type;
    p.component = n.component;
    return p;
}

bool isContainer(NodeType type) {
    return type == NodeType::Frame || type == NodeType::Component || type == NodeType::Instance;
}

bool canHaveChildren(NodeType type) {
    return type == NodeType::Frame || type == NodeType::Component;
}

} // namespace vc
