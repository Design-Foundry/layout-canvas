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
    swapIn(inverse.cornerRadius, patch.cornerRadius, node.cornerRadius);
    swapIn(inverse.opacity, patch.opacity, node.opacity);
    swapIn(inverse.visible, patch.visible, node.visible);
    swapIn(inverse.clipContent, patch.clipContent, node.clipContent);
    return inverse;
}

bool canHaveChildren(NodeType type) {
    return type == NodeType::Frame;
}

} // namespace vc
