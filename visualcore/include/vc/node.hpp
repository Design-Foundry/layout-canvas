#pragma once
#include <optional>
#include <string>
#include "vc/color.hpp"
#include "vc/id.hpp"
#include "vc/value.hpp"

namespace vc {

enum class NodeType {
    Frame,
    Rect,
    Ellipse,
    /// Reserved for component instances from a Design Foundry release.
    Instance,
};

struct Node {
    Id id;
    NodeType type = NodeType::Rect;
    /// Nil for top-level nodes.
    Id parent;
    /// Fractional index among siblings.
    std::string order;

    std::string name;
    /// Position relative to the parent, in world units.
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;
    Value<Oklch> fill = Oklch{};
    Value<double> cornerRadius = 0.0;
    double opacity = 1.0;
    bool visible = true;
    bool clipContent = false;

    friend bool operator==(const Node&, const Node&) = default;
};

/// A partial update of a node's properties. Unset fields stay as they are.
struct NodePatch {
    std::optional<std::string> name;
    std::optional<double> x;
    std::optional<double> y;
    std::optional<double> w;
    std::optional<double> h;
    std::optional<Value<Oklch>> fill;
    std::optional<Value<double>> cornerRadius;
    std::optional<double> opacity;
    std::optional<bool> visible;
    std::optional<bool> clipContent;

    friend bool operator==(const NodePatch&, const NodePatch&) = default;
};

/// Applies `patch` to `node` and returns the patch that restores the previous values.
NodePatch applyPatch(Node& node, const NodePatch& patch);

[[nodiscard]] bool canHaveChildren(NodeType type);

} // namespace vc
