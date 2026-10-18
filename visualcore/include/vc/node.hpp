#pragma once
#include <array>
#include <optional>
#include <string>
#include <vector>
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

/// Sides and corners are listed in this order everywhere: top, end, bottom, start (DF's logical
/// order, left to right as end = right). Corners: top-start, top-end, bottom-end, bottom-start.
enum Side { kTop = 0, kEnd = 1, kBottom = 2, kStart = 3 };

enum class StrokeStyle { None, Solid, Dashed, Dotted, Double };
enum class StrokeAlign { Inside, Center, Outside };

/// A border. `sides` overrides `width` per side; unset sides use `width`.
struct Stroke {
    Value<double> width = 0.0;
    std::array<std::optional<Value<double>>, 4> sides{};
    StrokeStyle style = StrokeStyle::Solid;
    Value<Oklch> color = Oklch{0.0, 0.0, 0.0, 1.0};
    StrokeAlign align = StrokeAlign::Inside;

    friend bool operator==(const Stroke&, const Stroke&) = default;
};

/// One shadow layer, drawn outside the shape or, when `inset`, inside it.
struct Shadow {
    double x = 0.0;
    double y = 4.0;
    double blur = 8.0;
    double spread = 0.0;
    Value<Oklch> color = Oklch{0.0, 0.0, 0.0, 0.25};
    bool inset = false;
    bool visible = true;

    friend bool operator==(const Shadow&, const Shadow&) = default;
};

enum class LayoutType { None, Row, Column };
enum class Justify { Start, Center, End, SpaceBetween, SpaceAround, SpaceEvenly };
enum class Align { Start, Center, End, Stretch };

/// Auto layout of a frame's children, after DF's surface layout and Figma's auto layout.
struct Layout {
    LayoutType type = LayoutType::None;
    Value<double> gap = 0.0;
    std::array<Value<double>, 4> padding{0.0, 0.0, 0.0, 0.0};
    Justify justify = Justify::Start;
    Align align = Align::Start;

    friend bool operator==(const Layout&, const Layout&) = default;
};

/// How a node sizes along one axis: its own size, its content (frames with auto layout), or the
/// space its auto-layout parent gives it.
enum class Sizing { Fixed, Hug, Fill };

struct SizeLimits {
    std::optional<Value<double>> minWidth;
    std::optional<Value<double>> maxWidth;
    std::optional<Value<double>> minHeight;
    std::optional<Value<double>> maxHeight;

    friend bool operator==(const SizeLimits&, const SizeLimits&) = default;
};

struct Node {
    Id id;
    NodeType type = NodeType::Rect;
    /// Nil for top-level nodes.
    Id parent;
    /// Fractional index among siblings.
    std::string order;

    std::string name;
    /// Position relative to the parent, in world units. Ignored for children placed by auto layout.
    double x = 0.0;
    double y = 0.0;
    /// Size when the axis is `Sizing::Fixed`; otherwise the last size the user set.
    double w = 0.0;
    double h = 0.0;

    Value<Oklch> fill = Oklch{};
    bool fillVisible = true;
    Value<double> cornerRadius = 0.0;
    /// Per-corner overrides of `cornerRadius`.
    std::array<std::optional<Value<double>>, 4> cornerRadii{};
    /// A literal stroke, or a reference to a border token. Width 0 or style none draws nothing.
    Value<Stroke> stroke = Stroke{};
    /// Literal layers, or a reference to a shadow token. Empty draws nothing.
    Value<std::vector<Shadow>> shadows = std::vector<Shadow>{};
    Value<double> opacity = 1.0;
    bool visible = true;
    bool locked = false;
    bool clipContent = false;

    Layout layout;
    Sizing widthSizing = Sizing::Fixed;
    Sizing heightSizing = Sizing::Fixed;
    SizeLimits limits;
    /// Inside an auto-layout parent, keeps its own position instead of joining the flow.
    bool absolute = false;

    friend bool operator==(const Node&, const Node&) = default;
};

/// A partial update of a node's properties. Unset fields stay as they are. Grouped properties
/// (stroke, shadows, layout, limits, corner radii) are replaced whole.
struct NodePatch {
    std::optional<std::string> name;
    std::optional<double> x;
    std::optional<double> y;
    std::optional<double> w;
    std::optional<double> h;
    std::optional<Value<Oklch>> fill;
    std::optional<bool> fillVisible;
    std::optional<Value<double>> cornerRadius;
    std::optional<std::array<std::optional<Value<double>>, 4>> cornerRadii;
    std::optional<Value<Stroke>> stroke;
    std::optional<Value<std::vector<Shadow>>> shadows;
    std::optional<Value<double>> opacity;
    std::optional<bool> visible;
    std::optional<bool> locked;
    std::optional<bool> clipContent;
    std::optional<Layout> layout;
    std::optional<Sizing> widthSizing;
    std::optional<Sizing> heightSizing;
    std::optional<SizeLimits> limits;
    std::optional<bool> absolute;

    friend bool operator==(const NodePatch&, const NodePatch&) = default;
};

/// Applies `patch` to `node` and returns the patch that restores the previous values.
NodePatch applyPatch(Node& node, const NodePatch& patch);

/// The patch that sets every property of `node`, for writing a whole node.
[[nodiscard]] NodePatch fullPatch(const Node& node);

[[nodiscard]] bool canHaveChildren(NodeType type);

enum class TokenFamilyHint { Color, Dimension, Opacity, Border, Shadow };

/// Calls `fn(tokenId, expectedFamily)` for every token a patch refers to.
template <typename Fn>
void forEachPatchTokenRef(const NodePatch& patch, Fn&& fn) {
    const auto visit = [&](const auto& value, TokenFamilyHint family) {
        if (const auto* ref = std::get_if<TokenRef>(&value)) fn(ref->token, family);
    };
    const auto visitColor = [&](const Value<Oklch>& value) { visit(value, TokenFamilyHint::Color); };
    const auto visitLength = [&](const Value<double>& value) { visit(value, TokenFamilyHint::Dimension); };

    if (patch.fill) visitColor(*patch.fill);
    if (patch.cornerRadius) visitLength(*patch.cornerRadius);
    if (patch.cornerRadii) {
        for (const auto& corner : *patch.cornerRadii) {
            if (corner) visitLength(*corner);
        }
    }
    if (patch.stroke) {
        visit(*patch.stroke, TokenFamilyHint::Border);
        if (const auto* stroke = std::get_if<Stroke>(&*patch.stroke)) {
            visitLength(stroke->width);
            for (const auto& side : stroke->sides) {
                if (side) visitLength(*side);
            }
            visitColor(stroke->color);
        }
    }
    if (patch.shadows) {
        visit(*patch.shadows, TokenFamilyHint::Shadow);
        if (const auto* layers = std::get_if<std::vector<Shadow>>(&*patch.shadows)) {
            for (const Shadow& layer : *layers) visitColor(layer.color);
        }
    }
    if (patch.opacity) visit(*patch.opacity, TokenFamilyHint::Opacity);
    if (patch.layout) {
        visitLength(patch.layout->gap);
        for (const auto& side : patch.layout->padding) visitLength(side);
    }
    if (patch.limits) {
        for (const auto* limit : {&patch.limits->minWidth, &patch.limits->maxWidth, &patch.limits->minHeight,
                                  &patch.limits->maxHeight}) {
            if (*limit) visitLength(**limit);
        }
    }
}

} // namespace vc
