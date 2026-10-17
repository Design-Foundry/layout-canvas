#pragma once
#include <optional>
#include <unordered_map>
#include <vector>
#include "vc/node.hpp"
#include "vc/op.hpp"

namespace vc {

/// The node tree. Ops are the only way to change it.
class Document {
public:
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

    /// True if `ancestor` is `id` itself or one of its ancestors.
    [[nodiscard]] bool isAncestorOrSelf(const Id& ancestor, const Id& id) const;

private:
    std::optional<OpBody> create(const CreateNode& op);
    std::optional<OpBody> remove(const DeleteNode& op);
    std::optional<OpBody> setProps(const SetProps& op);
    std::optional<OpBody> move(const MoveNode& op);

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
};

} // namespace vc
