#include "vc/document.hpp"
#include "vc/layout.hpp"

#include <unordered_set>

#include <algorithm>
#include <type_traits>

namespace vc {
namespace {

bool nonNegative(const Value<double>& value) {
    const auto* literal = std::get_if<double>(&value);
    return !literal || *literal >= 0.0;
}

bool isValidPatch(const NodePatch& patch) {
    if ((patch.w && *patch.w < 0.0) || (patch.h && *patch.h < 0.0)) return false;
    if (patch.opacity) {
        const auto* opacity = std::get_if<double>(&*patch.opacity);
        if (opacity && (*opacity < 0.0 || *opacity > 1.0)) return false;
    }
    if (patch.cornerRadius && !nonNegative(*patch.cornerRadius)) return false;
    if (patch.cornerRadii) {
        for (const auto& corner : *patch.cornerRadii) {
            if (corner && !nonNegative(*corner)) return false;
        }
    }
    if (patch.stroke) {
        if (const auto* stroke = std::get_if<Stroke>(&*patch.stroke)) {
            if (!nonNegative(stroke->width)) return false;
            for (const auto& side : stroke->sides) {
                if (side && !nonNegative(*side)) return false;
            }
        }
    }
    if (patch.shadows) {
        if (const auto* layers = std::get_if<std::vector<Shadow>>(&*patch.shadows)) {
            for (const Shadow& layer : *layers) {
                if (layer.blur < 0.0) return false;
            }
        }
    }
    if (patch.layout) {
        if (!nonNegative(patch.layout->gap)) return false;
        for (const auto& side : patch.layout->padding) {
            if (!nonNegative(side)) return false;
        }
    }
    if (patch.limits) {
        for (const auto* limit : {&patch.limits->minWidth, &patch.limits->maxWidth, &patch.limits->minHeight,
                                  &patch.limits->maxHeight}) {
            if (*limit && !nonNegative(**limit)) return false;
        }
    }
    return true;
}

} // namespace

Document::Document() {
    modes_.emplace(kDefaultModeId, Mode{kDefaultModeId, "Default", "a0"});
}

std::optional<OpBody> Document::apply(const OpBody& op) {
    auto inverse = std::visit(
        [this](const auto& body) -> std::optional<OpBody> {
            using T = std::decay_t<decltype(body)>;
            if constexpr (std::is_same_v<T, CreateNode>) {
                return create(body);
            } else if constexpr (std::is_same_v<T, DeleteNode>) {
                return remove(body);
            } else if constexpr (std::is_same_v<T, SetProps>) {
                return setProps(body);
            } else if constexpr (std::is_same_v<T, MoveNode>) {
                return move(body);
            } else if constexpr (std::is_same_v<T, CreateToken>) {
                return createToken(body);
            } else if constexpr (std::is_same_v<T, DeleteToken>) {
                return deleteToken(body);
            } else if constexpr (std::is_same_v<T, ReplaceToken>) {
                return replaceToken(body);
            } else if constexpr (std::is_same_v<T, CreateMode>) {
                return createMode(body);
            } else if constexpr (std::is_same_v<T, DeleteMode>) {
                return deleteMode(body);
            } else if constexpr (std::is_same_v<T, ReplaceMode>) {
                return replaceMode(body);
            } else {
                return setOverride(body);
            }
        },
        op);
    if (inverse) {
        layoutDirty_ = true;
        treeDirty_ = true;
    }
    return inverse;
}

Rect Document::box(const Id& id, const Id& mode) const {
    if (layoutDirty_ || layoutMode_ != mode) {
        layout_ = computeLayout(*this, mode);
        layoutMode_ = mode;
        layoutDirty_ = false;
    }
    const auto it = layout_.find(id);
    return it == layout_.end() ? Rect{} : it->second;
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
        !isValidPatch(fullPatch(node)) || !isValidNodeRefs(node)) {
        return std::nullopt;
    }
    if (node.type == NodeType::Instance && !isValidInstance(node)) {
        return std::nullopt;
    }
    for (const Override& o : node.overrides) {
        if (!isValidPatch(o.patch) || !isValidPatchRefs(o.patch)) return std::nullopt;
    }
    link(nodes_.emplace(node.id, node).first->second);
    return DeleteNode{node.id};
}

std::optional<OpBody> Document::remove(const DeleteNode& op) {
    const auto it = nodes_.find(op.id);
    if (it == nodes_.end() || !children(op.id).empty() ||
        (it->second.type == NodeType::Component && isComponentUsed(op.id))) {
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
    if (it == nodes_.end() || !isValidPatch(op.patch) || !isValidPatchRefs(op.patch)) {
        return std::nullopt;
    }
    const Node& current = it->second;
    if (op.patch.type && *op.patch.type != current.type) {
        const bool frameOrComponent = [](NodeType t) { return t == NodeType::Frame || t == NodeType::Component; }(
            *op.patch.type);
        if (!frameOrComponent || (current.type != NodeType::Frame && current.type != NodeType::Component)) {
            return std::nullopt;
        }
        if (current.type == NodeType::Component && isComponentUsed(op.id)) {
            return std::nullopt;
        }
    }
    if (op.patch.component) {
        Node candidate = current;
        candidate.component = *op.patch.component;
        if (current.type != NodeType::Instance || !isValidInstance(candidate)) return std::nullopt;
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
    bool cycle = false;
    const auto checkSubtree = [&](const Id& id, auto&& self) -> void {
        const Node& n = nodes_.at(id);
        if (n.type == NodeType::Instance && wouldCycle(op.parent, n.component)) cycle = true;
        if (n.type == NodeType::Component && !enclosingComponent(op.parent).isNil()) cycle = true;
        for (const Id& child : children(id)) self(child, self);
    };
    checkSubtree(op.id, checkSubtree);
    if (cycle) {
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

namespace {

constexpr int kMaxInstanceDepth = 16;

std::uint64_t mixBits(std::uint64_t x) {
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBull;
    x ^= x >> 31;
    return x;
}

/// A stable id for `node` copied inside `instance`.
Id copyKey(const Id& instance, const Id& node) {
    return {mixBits(instance.hi ^ mixBits(node.hi + 0x9E3779B97F4A7C15ull)),
            mixBits(instance.lo ^ mixBits(node.lo + 0xD1B54A32D192ED03ull))};
}

const NodePatch* findOverride(const std::vector<Override>& overrides, const Id& node) {
    for (const Override& o : overrides) {
        if (o.node == node) return &o.patch;
    }
    return nullptr;
}

} // namespace

std::optional<OpBody> Document::setOverride(const SetOverride& op) {
    const auto it = nodes_.find(op.instance);
    if (it == nodes_.end() || it->second.type != NodeType::Instance || !isValidPatch(op.patch) ||
        !isValidPatchRefs(op.patch) || op.patch.type || op.patch.component) {
        return std::nullopt;
    }
    auto& overrides = it->second.overrides;
    const auto existing = std::find_if(overrides.begin(), overrides.end(), [&](const Override& o) { return o.node == op.node; });
    SetOverride inverse{op.instance, op.node, existing == overrides.end() ? NodePatch{} : existing->patch};
    if (existing != overrides.end()) {
        overrides.erase(existing);
    }
    if (op.patch != NodePatch{}) {
        const auto at = std::lower_bound(overrides.begin(), overrides.end(), op.node,
                                         [](const Override& o, const Id& id) { return o.node < id; });
        overrides.insert(at, Override{op.node, op.patch});
    }
    return inverse;
}

Id Document::enclosingComponent(const Id& id) const {
    for (const Node* node = find(id); node; node = find(node->parent)) {
        if (node->type == NodeType::Component) return node->id;
    }
    return {};
}

bool Document::componentUses(const Id& component, const Id& target) const {
    std::unordered_set<Id> seen;
    const auto visit = [&](const Id& id, auto&& self) -> bool {
        const Node* node = find(id);
        if (!node) return false;
        if (node->type == NodeType::Instance) {
            if (node->component == target) return true;
            if (seen.insert(node->component).second && self(node->component, self)) return true;
        }
        for (const Id& child : children(id)) {
            if (self(child, self)) return true;
        }
        return false;
    };
    return visit(component, visit);
}

bool Document::wouldCycle(const Id& parent, const Id& used) const {
    const Id host = enclosingComponent(parent);
    return !host.isNil() && (host == used || componentUses(used, host));
}

bool Document::isValidInstance(const Node& node) const {
    const Node* component = find(node.component);
    return component && component->type == NodeType::Component && !wouldCycle(node.parent, node.component);
}

bool Document::isComponentUsed(const Id& component) const {
    for (const auto& [id, node] : nodes_) {
        if (node.type == NodeType::Instance && node.component == component) return true;
    }
    return false;
}

const Node* Document::view(const Id& key) const {
    buildTree();
    const auto it = tree_.find(key);
    return it == tree_.end() ? nullptr : it->second.node;
}

const std::vector<Id>& Document::viewChildren(const Id& key) const {
    static const std::vector<Id> kNone;
    buildTree();
    const auto it = treeChildren_.find(key);
    return it == treeChildren_.end() ? kNone : it->second;
}

Id Document::ownerOf(const Id& key) const {
    buildTree();
    const auto it = tree_.find(key);
    return it == tree_.end() ? Id{} : it->second.owner;
}

Id Document::overrideKeyOf(const Id& key) const {
    buildTree();
    const auto it = tree_.find(key);
    return it == tree_.end() ? Id{} : it->second.overrideKey;
}

Id Document::sourceOf(const Id& key) const {
    buildTree();
    const auto it = tree_.find(key);
    return it == tree_.end() ? Id{} : it->second.source;
}

void Document::buildTree() const {
    if (!treeDirty_) {
        return;
    }
    tree_.clear();
    treeChildren_.clear();
    treeNodes_.clear();
    const auto walk = [this](const Id& parent, auto&& self) -> void {
        for (const Id& id : children(parent)) {
            const Node& node = nodes_.at(id);
            if (node.type == NodeType::Instance) {
                expand(id, id, parent, id, {}, 0);
            } else {
                tree_[id] = {&node, id, id, {}};
                treeChildren_[parent].push_back(id);
                self(id, self);
            }
        }
    };
    walk(Id{}, walk);
    treeDirty_ = false;
}

void Document::expand(const Id& source, const Id& key, const Id& parent, const Id& owner,
                      const std::vector<OverrideLayer>& layers, int depth) const {
    const Node& original = nodes_.at(source);
    const auto keyIn = [&](const OverrideLayer& layer) {
        return layer.prefix.isNil() ? source : copyKey(layer.prefix, source);
    };
    const auto applyLayers = [&](Node& node) {
        for (const OverrideLayer& layer : layers) {
            if (const NodePatch* patch = findOverride(*layer.overrides, keyIn(layer))) applyPatch(node, *patch);
        }
    };

    Node effective = original;
    std::vector<OverrideLayer> inner = layers;
    Id childrenOf = source;
    const Node* component = original.type == NodeType::Instance ? find(original.component) : nullptr;
    if (component && depth < kMaxInstanceDepth) {
        effective = *component;
        if (const NodePatch* patch = findOverride(original.overrides, component->id)) applyPatch(effective, *patch);
        effective.x = original.x;
        effective.y = original.y;
        effective.w = original.w;
        effective.h = original.h;
        effective.widthSizing = original.widthSizing;
        effective.heightSizing = original.heightSizing;
        effective.limits = original.limits;
        effective.absolute = original.absolute;
        effective.visible = original.visible && effective.visible;
        effective.locked = original.locked;
        effective.name = original.name;
        effective.order = original.order;
        applyLayers(effective);
        inner = {{&original.overrides, {}}};
        for (const OverrideLayer& layer : layers) inner.push_back({layer.overrides, keyIn(layer)});
        childrenOf = component->id;
    } else {
        applyLayers(effective);
        if (original.type == NodeType::Instance) childrenOf = Id{};
    }
    if (original.type == NodeType::Instance) {
        effective.type = NodeType::Instance;
        effective.component = original.component;
    }
    effective.id = key;
    effective.parent = parent;
    treeNodes_.push_back(std::move(effective));
    tree_[key] = {&treeNodes_.back(), source, owner, layers.empty() ? Id{} : keyIn(layers.back())};
    treeChildren_[parent].push_back(key);

    if (childrenOf.isNil()) {
        return;
    }
    for (const Id& child : children(childrenOf)) {
        expand(child, copyKey(key, child), key, owner, inner, depth + 1);
    }
}

} // namespace vc
