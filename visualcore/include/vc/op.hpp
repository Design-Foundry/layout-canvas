#pragma once
#include <string>
#include <variant>
#include "vc/node.hpp"

namespace vc {

struct CreateNode {
    Node node;
    friend bool operator==(const CreateNode&, const CreateNode&) = default;
};

/// Removes a node that has no children.
struct DeleteNode {
    Id id;
    friend bool operator==(const DeleteNode&, const DeleteNode&) = default;
};

struct SetProps {
    Id id;
    NodePatch patch;
    friend bool operator==(const SetProps&, const SetProps&) = default;
};

/// Reparents and/or reorders a node.
struct MoveNode {
    Id id;
    Id parent;
    std::string order;
    friend bool operator==(const MoveNode&, const MoveNode&) = default;
};

using OpBody = std::variant<CreateNode, DeleteNode, SetProps, MoveNode>;

/// An edit as it travels to the server: a client-generated id plus the change.
struct Op {
    Id id;
    OpBody body;
    friend bool operator==(const Op&, const Op&) = default;
};

} // namespace vc
