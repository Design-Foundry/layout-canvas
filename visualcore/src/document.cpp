#include "vc/document.hpp"

#include <algorithm>
#include <type_traits>

namespace vc {
namespace {

bool isValidGeometry(double w, double h, double opacity) {
    return w >= 0.0 && h >= 0.0 && opacity >= 0.0 && opacity <= 1.0;
}

bool isValidPatch(const NodePatch& patch) {
    return isValidGeometry(patch.w.value_or(0.0), patch.h.value_or(0.0), patch.opacity.value_or(1.0));
}

} // namespace

std::optional<OpBody> Document::apply(const OpBody& op) {
    return std::visit(
        [this](const auto& body) -> std::optional<OpBody> {
            using T = std::decay_t<decltype(body)>;
            if constexpr (std::is_same_v<T, CreateNode>) {
                return create(body);
            } else if constexpr (std::is_same_v<T, DeleteNode>) {
                return remove(body);
            } else if constexpr (std::is_same_v<T, SetProps>) {
                return setProps(body);
            } else {
                return move(body);
            }
        },
        op);
}

const Node* Document::find(const Id& id) const {
    const auto it = nodes_.find(id);
    return it == nodes_.end() ? nullptr : &it->second;
}

const std::vector<Id>& Document::children(const Id& parent) const {
    static const std::vector<Id> kNone;
    const auto it = children_.find(parent);
    return it == children_.end() ? kNone : it->second;
}

bool Document::isAncestorOrSelf(const Id& ancestor, const Id& id) const {
    for (const Node* node = find(id); node; node = find(node->parent)) {
        if (node->id == ancestor) {
            return true;
        }
    }
    return false;
}

std::optional<OpBody> Document::create(const CreateNode& op) {
    const Node& node = op.node;
    if (node.id.isNil() || nodes_.contains(node.id) || !isValidParent(node.parent) ||
        !isValidGeometry(node.w, node.h, node.opacity)) {
        return std::nullopt;
    }
    link(nodes_.emplace(node.id, node).first->second);
    return DeleteNode{node.id};
}

std::optional<OpBody> Document::remove(const DeleteNode& op) {
    const auto it = nodes_.find(op.id);
    if (it == nodes_.end() || !children(op.id).empty()) {
        return std::nullopt;
    }
    CreateNode inverse{it->second};
    unlink(it->second);
    children_.erase(op.id);
    nodes_.erase(it);
    return inverse;
}

std::optional<OpBody> Document::setProps(const SetProps& op) {
    const auto it = nodes_.find(op.id);
    if (it == nodes_.end() || !isValidPatch(op.patch)) {
        return std::nullopt;
    }
    return SetProps{op.id, applyPatch(it->second, op.patch)};
}

std::optional<OpBody> Document::move(const MoveNode& op) {
    const auto it = nodes_.find(op.id);
    if (it == nodes_.end() || !isValidParent(op.parent)) {
        return std::nullopt;
    }
    if (!op.parent.isNil() && isAncestorOrSelf(op.id, op.parent)) {
        return std::nullopt;
    }
    Node& node = it->second;
    MoveNode inverse{op.id, node.parent, node.order};
    unlink(node);
    node.parent = op.parent;
    node.order = op.order;
    link(node);
    return inverse;
}

void Document::link(const Node& node) {
    auto& siblings = children_[node.parent];
    const auto paintsBefore = [this](const Id& a, const Id& b) {
        const Node& na = nodes_.at(a);
        const Node& nb = nodes_.at(b);
        return na.order != nb.order ? na.order < nb.order : na.id < nb.id;
    };
    siblings.insert(std::upper_bound(siblings.begin(), siblings.end(), node.id, paintsBefore), node.id);
}

void Document::unlink(const Node& node) {
    const auto it = children_.find(node.parent);
    if (it == children_.end()) {
        return;
    }
    std::erase(it->second, node.id);
    if (it->second.empty()) {
        children_.erase(it);
    }
}

bool Document::isValidParent(const Id& parent) const {
    if (parent.isNil()) {
        return true;
    }
    const Node* node = find(parent);
    return node && canHaveChildren(node->type);
}

} // namespace vc
