#pragma once
#include <array>
#include <deque>
#include <memory>
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
    /// Independent authored data with fresh expansion/layout caches. Presentation
    /// can resolve temporary instance values without modifying the source document.
    [[nodiscard]] std::shared_ptr<Document> detachedCopy() const {
        auto copy=std::make_shared<Document>();
        copy->nodes_=nodes_;copy->children_=children_;
        copy->tokens_=tokens_;copy->modes_=modes_;
        copy->playbackValues_=playbackValues_;
        return copy;
    }
    /// Ephemeral variant axes on one expanded instance occurrence. Never serialized or
    /// applied as an authored operation; presentation uses this only on detached documents.
    bool setPlaybackState(const Id& key, const std::map<std::string,std::string>& values);

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

    /// The expanded tree: the document's nodes plus a copy of each instance's component inside it.
    /// Keys are node ids for real nodes and stable derived ids for copies. The effective node has
    /// overrides applied; instances appear as frames. Rebuilt lazily after each edit.
    [[nodiscard]] const Node* view(const Id& key) const;
    [[nodiscard]] const std::vector<Id>& viewChildren(const Id& key) const;
    /// The real node a key belongs to: itself, or the outermost instance it was copied into.
    [[nodiscard]] Id ownerOf(const Id& key) const;
    /// The document node a key was copied from (itself for real nodes).
    [[nodiscard]] Id sourceOf(const Id& key) const;
    /// The node id an override on the owning instance uses for a copy: the source id for nodes of
    /// the instance's own component, a path through nested instances otherwise. Nil for real nodes.
    [[nodiscard]] Id overrideKeyOf(const Id& key) const;
    /// True if an instance uses `component`.
    [[nodiscard]] bool isComponentUsed(const Id& component) const;
    [[nodiscard]] bool isSwapTarget(const Id& id) const;
    [[nodiscard]] bool isTextTarget(const Id& id) const;

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
    std::optional<OpBody> setOverride(const SetOverride& op);
    std::optional<OpBody> setComponentDefinition(const SetComponentDefinition& op);
    [[nodiscard]] bool validDefinition(const Id& component, const ComponentDefinition& definition) const;
    [[nodiscard]] bool validComponentValues(const ComponentDefinition& definition, const ComponentValues& values) const;
    [[nodiscard]] bool definitionTargets(const Id& id) const;
    [[nodiscard]] bool validInteraction(const Interaction& interaction) const;
    [[nodiscard]] bool prototypeReferences(const Id& target) const;
    [[nodiscard]] bool hasDependentOverrides(const Id& component) const;
    [[nodiscard]] bool swapValuesWouldStrandOverrides(const Node& instance, const ComponentValues& values) const;

    /// The component `id` sits in (itself if it is one), or nil.
    [[nodiscard]] Id enclosingComponent(const Id& id) const;
    /// True if `component`'s subtree contains an instance of `target`, directly or through other
    /// instances.
    [[nodiscard]] bool componentUses(const Id& component, const Id& target) const;
    /// True if placing an instance of `used` under `parent` would make a component contain itself.
    [[nodiscard]] bool wouldCycle(const Id& parent, const Id& used) const;
    [[nodiscard]] bool isValidInstance(const Node& node) const;
    void buildTree() const;
    /// One instance's overrides as seen from inside it: `prefix` is the path of the nested instance
    /// being expanded, nil within the instance's own component.
    struct OverrideLayer {
        const std::vector<Override>* overrides = nullptr;
        Id prefix;
        const ComponentDefinition* definition = nullptr;
        const ComponentValues* values = nullptr;
    };
    void expand(const Id& source, const Id& key, const Id& parent, const Id& owner,
                const std::vector<OverrideLayer>& layers, int depth) const;

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
    struct PlaybackValues { Id component; ComponentValues values; };
    std::unordered_map<Id, PlaybackValues> playbackValues_;

    struct TreeEntry {
        const Node* node = nullptr;
        Id source;
        Id owner;
        Id overrideKey;
    };
    mutable std::unordered_map<Id, TreeEntry> tree_;
    mutable std::unordered_map<Id, std::vector<Id>> treeChildren_;
    mutable std::deque<Node> treeNodes_;
    mutable bool treeDirty_ = true;

    mutable std::unordered_map<Id, Rect> layout_;
    mutable Id layoutMode_;
    mutable bool layoutDirty_ = true;
};

} // namespace vc
