#include "vc/node.hpp"
#include <cmath>

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

bool validPath(const Path& path) {
    if (path.points.size() < (path.closed ? 3u : 2u) || path.points.size() > 64) return false;
    if(path.filled && !path.closed) return false;
    if((!path.inHandles.empty() || !path.outHandles.empty()) &&
       (path.inHandles.size()!=path.points.size() || path.outHandles.size()!=path.points.size())) return false;
    for(const auto* list : {&path.points,&path.inHandles,&path.outHandles}) for(const auto& p:*list)
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || p.x < 0 || p.x > 1 || p.y < 0 || p.y > 1) return false;
    return true;
}

NodePatch applyPatch(Node& node, const NodePatch& patch) {
    NodePatch inverse;
    swapIn(inverse.image,patch.image,node.image);
    swapIn(inverse.imageFit,patch.imageFit,node.imageFit);
    swapIn(inverse.imageCrop,patch.imageCrop,node.imageCrop);
    swapIn(inverse.path, patch.path, node.path);
    swapIn(inverse.lineWidth, patch.lineWidth, node.lineWidth);
    swapIn(inverse.lineReverse, patch.lineReverse, node.lineReverse);
    swapIn(inverse.typography, patch.typography, node.typography);
    swapIn(inverse.text, patch.text, node.text);
    swapIn(inverse.textWrap, patch.textWrap, node.textWrap);
    if (patch.textStyle) {
        inverse.textStyle = TextStyle{};
        swapIn(inverse.textStyle->font, patch.textStyle->font, node.text.font);
        swapIn(inverse.textStyle->fontSize, patch.textStyle->fontSize, node.text.fontSize);
        swapIn(inverse.textStyle->lineHeight, patch.textStyle->lineHeight, node.text.lineHeight);
        swapIn(inverse.textStyle->letterSpacing, patch.textStyle->letterSpacing, node.text.letterSpacing);
        swapIn(inverse.textStyle->align, patch.textStyle->align, node.text.align);
    }
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
    swapIn(inverse.componentValues, patch.componentValues, node.componentValues);
    return inverse;
}

NodePatch mergePatch(const NodePatch& base, const NodePatch& top) {
    NodePatch out = base;
    if(top.image) out.image=top.image;
    if(top.imageFit) out.imageFit=top.imageFit;
    if(top.imageCrop) out.imageCrop=top.imageCrop;
    if (top.path) out.path = top.path;
    if (top.lineWidth) out.lineWidth = top.lineWidth;
    if (top.lineReverse) out.lineReverse = top.lineReverse;
    if (top.typography) out.typography = top.typography;
    if (top.textWrap) out.textWrap = top.textWrap;
    if (top.text) { out.text = top.text; out.textStyle.reset(); }
    const auto take = [](auto& field, const auto& value) {
        if (value) field = value;
    };
    if (top.textStyle) {
        if (out.text && !top.text) {
            Node node; node.text = *out.text;
            NodePatch style; style.textStyle = top.textStyle;
            applyPatch(node, style);
            out.text = node.text;
        } else {
            if (!out.textStyle) out.textStyle = TextStyle{};
            take(out.textStyle->fontSize, top.textStyle->fontSize);
            take(out.textStyle->lineHeight, top.textStyle->lineHeight);
            take(out.textStyle->letterSpacing, top.textStyle->letterSpacing);
            take(out.textStyle->align, top.textStyle->align);
            take(out.textStyle->font, top.textStyle->font);
        }
    }
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
    take(out.componentValues, top.componentValues);
    return out;
}

NodePatch fullPatch(const Node& n) {
    NodePatch p;
    if(n.type==NodeType::Image){p.image=n.image;p.imageFit=n.imageFit;p.imageCrop=n.imageCrop;}
    if (n.type == NodeType::Line || n.type == NodeType::Path) p.lineWidth=n.lineWidth;
    if (n.type == NodeType::Line) p.lineReverse=n.lineReverse;
    if (n.type == NodeType::Path) p.path=n.path;
    if (!n.typography.isNil()) p.typography = n.typography;
    if (n.type == NodeType::Text) p.text = n.text;
    if (n.type == NodeType::Text) p.textWrap = n.textWrap;
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
    p.componentValues = n.componentValues;
    return p;
}

bool isContainer(NodeType type) {
    return type == NodeType::Frame || type == NodeType::Component || type == NodeType::Instance;
}

bool canHaveChildren(NodeType type) {
    return type == NodeType::Frame || type == NodeType::Component;
}

} // namespace vc
