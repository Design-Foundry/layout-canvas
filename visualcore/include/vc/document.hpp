#pragma once
#include <array>
#include <optional>
#include <unordered_map>
#include <vector>
#include "vc/math.hpp"
#include "vc/node.hpp"
#include "vc/op.hpp"
#include "vc/tokens.hpp"

namespace vc {

/// A stroke ready to draw: widths per side in pixels (top, end, bottom, start).
struct ResolvedStroke {
    std::array<double, 4> widths{};
    StrokeStyle style = StrokeStyle::Solid;
    Oklch color;
    StrokeAlign align = StrokeAlign::Inside;
};

struct ResolvedShadow {
    double x = 0.0;
    double y = 0.0;
    double blur = 0.0;
    double spread = 0.0;
    Oklch color;
    bool inset = false;
};

/// The node tree and the document's design system (tokens and modes). Ops are the only way to change
/// either.
class Document {
public:
    Document();

    /// Applies `op` and returns its inverse, or nothing if the op is invalid for the current state
    /// (unknown node, cycle, deleting a node with children). Invalid ops leave the document untouched.
    std::optional<OpBody> apply(const OpBody& op);

    [[nodiscard]] const Node* find(const Id& id) const;
    /// Children of `parent` in sibling order. A nil `parent` lists the top-level nodes.
    [[nodiscard]] const std::vector<Id>& children(const Id& parent) const;
    [[nodiscard]] std::size_t size() const { return nodes_.size(); }

    /// Visits every node parent-first with siblings in order, which is the paint order.
    template <typename Fn>
    void forEachInPaintOrder(Fn&& fn) const {
        visit(Id{}, fn);
    }

    /// The node's box relative to its parent after auto layout, in `mode`. Cached until the next
    /// edit or a different mode.
    [[nodiscard]] Rect box(const Id& id, const Id& mode = kDefaultModeId) const;

    /// True if `ancestor` is `id` itself or one of its ancestors.
    [[nodiscard]] bool isAncestorOrSelf(const Id& ancestor, const Id& id) const;

    [[nodiscard]] const Token* findToken(const Id& id) const;
    /// All tokens, grouped by family and in each family's order.
    [[nodiscard]] std::vector<const Token*> tokens() const;
    [[nodiscard]] const Mode* findMode(const Id& id) const;
    /// All modes in order. The default mode is always present.
    [[nodiscard]] std::vector<const Mode*> modes() const;
    /// True if a node or another token refers to `token`.
    [[nodiscard]] bool isTokenReferenced(const Id& token) const;

    /// The token's value in `mode` after aliases and modifiers. Colors carry their alpha. Nothing if
    /// the token doesn't exist or can't be resolved. An unknown mode falls back to the default.
    [[nodiscard]] std::optional<TokenLiteral> resolveToken(const Id& token, const Id& mode) const;
    [[nodiscard]] std::optional<Oklch> resolveColor(const Value<Oklch>& value, const Id& mode) const;
    /// A length in canvas pixels. Dimension tokens convert: rem and em as 16px, pt as 4/3px.
    [[nodiscard]] std::optional<double> resolveLength(const Value<double>& value, const Id& mode) const;
    /// Opacity in [0, 1]; an unresolvable token counts as fully opaque.
    [[nodiscard]] double resolveOpacity(const Value<double>& value, const Id& mode) const;
    /// Corner radii in pixels: top-start, top-end, bottom-end, bottom-start.
    [[nodiscard]] std::array<double, 4> resolveCorners(const Node& node, const Id& mode) const;
    /// Nothing when the stroke draws nothing (no width, style none, or unresolvable).
    [[nodiscard]] std::optional<ResolvedStroke> resolveStroke(const Value<Stroke>& value, const Id& mode) const;
    [[nodiscard]] std::vector<ResolvedShadow> resolveShadows(const Value<std::vector<Shadow>>& value,
                                                             const Id& mode) const;

private:
    std::optional<OpBody> create(const CreateNode& op);
    std::optional<OpBody> remove(const DeleteNode& op);
    std::optional<OpBody> setProps(const SetProps& op);
    std::optional<OpBody> move(const MoveNode& op);
    std::optional<OpBody> createToken(const CreateToken& op);
    std::optional<OpBody> deleteToken(const DeleteToken& op);
    std::optional<OpBody> replaceToken(const ReplaceToken& op);
    std::optional<OpBody> createMode(const CreateMode& op);
    std::optional<OpBody> deleteMode(const DeleteMode& op);
    std::optional<OpBody> replaceMode(const ReplaceMode& op);

    [[nodiscard]] bool isValidToken(const Token& token) const;
    [[nodiscard]] bool isValidNodeRefs(const Node& node) const;
    [[nodiscard]] bool isValidPatchRefs(const NodePatch& patch) const;
    [[nodiscard]] bool hasCycle(const Token& candidate) const;
    [[nodiscard]] bool isValidColorModifiers(const std::vector<Modifier>& modifiers) const;
    bool applyColorModifiers(Oklch& color, const std::vector<Modifier>& modifiers, const Id& mode, int depth) const;
    [[nodiscard]] std::optional<TokenLiteral> resolveToken(const Id& token, const Id& mode, int depth) const;

    void link(const Node& node);
    void unlink(const Node& node);
    [[nodiscard]] bool isValidParent(const Id& parent) const;

    template <typename Fn>
    void visit(const Id& parent, Fn& fn) const {
        for (const Id& id : children(parent)) {
            fn(nodes_.at(id));
            visit(id, fn);
        }
    }

    std::unordered_map<Id, Node> nodes_;
    std::unordered_map<Id, std::vector<Id>> children_;
    std::unordered_map<Id, Token> tokens_;
    std::unordered_map<Id, Mode> modes_;

    mutable std::unordered_map<Id, Rect> layout_;
    mutable Id layoutMode_;
    mutable bool layoutDirty_ = true;
};

} // namespace vc
