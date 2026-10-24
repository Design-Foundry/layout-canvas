#pragma once
#include "vc/text.hpp"
#include "vc/math.hpp"
#include "vc/raster.hpp"
#include <array>
#include <map>
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
    /// A frame that defines a component: its children are the component's anatomy.
    Component,
    /// A copy of a component that follows it, with its own size and overrides.
    Instance,
    Text,
    Line,
    Path,
    Image,
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

using ComponentValue = std::variant<bool, std::string>;
using ComponentValues = std::map<std::string, ComponentValue>;

/// A partial update of a node's properties. Unset fields stay as they are. Grouped properties
/// (stroke, shadows, layout, limits, corner radii) are replaced whole.
struct Path {
    std::vector<Vec2> points;
    bool closed = false;
    // Empty on straight paths. Otherwise both lists match points, with absolute normalized controls.
    std::vector<Vec2> inHandles{};
    std::vector<Vec2> outHandles{};
    bool filled = false;
    friend bool operator==(const Path& a, const Path& b) {
        const auto equal = [](const auto& x,const auto& y) {
            if(x.size()!=y.size()) return false;
            for(std::size_t i=0;i<x.size();++i) if(x[i].x!=y[i].x || x[i].y!=y[i].y) return false;
            return true;
        };
        return a.closed==b.closed && a.filled==b.filled && equal(a.points,b.points) && equal(a.inHandles,b.inHandles) && equal(a.outHandles,b.outHandles);
    }
};
[[nodiscard]] bool validPath(const Path& path);
/// Why `validPath` refuses `path`, or null when it accepts it.
[[nodiscard]] const char* pathProblem(const Path& path);

struct ImageCrop : Rect {
    ImageCrop() : Rect{0,0,1,1} {}
    ImageCrop(double x,double y,double w,double h) : Rect{x,y,w,h} {}
    friend bool operator==(const ImageCrop& a,const ImageCrop& b){return a.x==b.x&&a.y==b.y&&a.w==b.w&&a.h==b.h;}
};
struct NodePatch {
    std::optional<Raster> image;
    std::optional<std::string> imageFit;
    std::optional<ImageCrop> imageCrop;
    std::optional<Path> path;
    std::optional<double> lineWidth;
    std::optional<bool> lineReverse;
    /// Source text style binding. Nil clears the binding; copied-layer binding changes are refused.
    std::optional<Id> typography;
    std::optional<TextRun> text;
    std::optional<bool> textWrap;
    std::optional<TextStyle> textStyle;
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
    /// Only between frame and component.
    std::optional<NodeType> type;
    /// For instances: swaps the component they copy.
    std::optional<Id> component;
    /// Explicit declared property/axis values on an instance; empty restores defaults.
    std::optional<ComponentValues> componentValues;

    friend bool operator==(const NodePatch&, const NodePatch&) = default;
};

struct BooleanProperty {
    std::string name;
    Id target;
    bool defaultValue = true;
    friend bool operator==(const BooleanProperty&, const BooleanProperty&) = default;
};

struct VariantAxis {
    std::string name;
    std::vector<std::string> values;
    std::string defaultValue;
    friend bool operator==(const VariantAxis&, const VariantAxis&) = default;
};

struct InstanceSwapProperty {
    std::string name;
    Id target;
    Id defaultComponent;
    std::vector<Id> choices;
    friend bool operator==(const InstanceSwapProperty&, const InstanceSwapProperty&) = default;
};

struct VariantRule {
    std::map<std::string, std::string> when;
    Id target;
    NodePatch patch;
    friend bool operator==(const VariantRule&, const VariantRule&) = default;
};

struct ComponentDefinition {
    std::vector<BooleanProperty> properties;
    std::vector<VariantAxis> axes;
    std::vector<VariantRule> rules;
    std::vector<InstanceSwapProperty> swaps;
    struct TextProperty {
        std::string name;
        Id target;
        std::string defaultValue;
        friend bool operator==(const TextProperty&, const TextProperty&) = default;
    };
    std::vector<TextProperty> texts;
    [[nodiscard]] bool empty() const { return properties.empty() && axes.empty() && rules.empty() && swaps.empty() && texts.empty(); }
    friend bool operator==(const ComponentDefinition&, const ComponentDefinition&) = default;
};

/// An instance's changes to one node of its component.
struct Override {
    Id node;
    NodePatch patch;

    friend bool operator==(const Override&, const Override&) = default;
};

struct Node {
    Raster image;
    std::string imageFit = "cover";
    ImageCrop imageCrop;
    Path path;
    double lineWidth = 2.0;
    bool lineReverse = false;
    Id typography;
    TextRun text;
    bool textWrap = false;
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

    /// For instances: the component they copy.
    Id component;
    /// For instances: property changes to the component's nodes, sorted by node id.
    std::vector<Override> overrides;
    ComponentDefinition definition;
    ComponentValues componentValues;

    friend bool operator==(const Node&, const Node&) = default;
};


/// Applies `patch` to `node` and returns the patch that restores the previous values.
NodePatch applyPatch(Node& node, const NodePatch& patch);

/// `base` with every field `top` sets taken from `top`.
[[nodiscard]] NodePatch mergePatch(const NodePatch& base, const NodePatch& top);

/// The patch that sets every property of `node`, for writing a whole node.
[[nodiscard]] NodePatch fullPatch(const Node& node);

/// Whether a node of this type can own children in the document.
[[nodiscard]] bool canHaveChildren(NodeType type);
/// Whether a node lays out, clips and draws children in the expanded tree: frames, components and
/// instances (whose children are copies of their component's).
[[nodiscard]] bool isContainer(NodeType type);

enum class TokenFamilyHint { Color, Dimension, Opacity, Border, Shadow, Typography };

/// Calls `fn(tokenId, expectedFamily)` for every token a patch refers to.
template <typename Fn>
void forEachPatchTokenRef(const NodePatch& patch, Fn&& fn) {
    if (patch.typography && !patch.typography->isNil()) fn(*patch.typography, TokenFamilyHint::Typography);
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
