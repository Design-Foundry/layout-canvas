#pragma once
#include "vc/font.hpp"
#include <string>
#include <variant>
#include "vc/node.hpp"
#include "vc/tokens.hpp"

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

struct CreateToken {
    Token token;
    friend bool operator==(const CreateToken&, const CreateToken&) = default;
};

/// Removes a token nothing references.
struct DeleteToken {
    Id id;
    friend bool operator==(const DeleteToken&, const DeleteToken&) = default;
};

/// Replaces a token's definition. Its id and family never change.
struct ReplaceToken {
    Token token;
    friend bool operator==(const ReplaceToken&, const ReplaceToken&) = default;
};

struct CreateMode {
    Mode mode;
    friend bool operator==(const CreateMode&, const CreateMode&) = default;
};

/// Removes a mode no alias names. The default mode can't be removed.
struct DeleteMode {
    Id id;
    friend bool operator==(const DeleteMode&, const DeleteMode&) = default;
};

struct ReplaceMode {
    Mode mode;
    friend bool operator==(const ReplaceMode&, const ReplaceMode&) = default;
};

struct CreateFont {
    Font font;
    friend bool operator==(const CreateFont&, const CreateFont&) = default;
};

/// Removes a font no stack lists.
struct DeleteFont {
    Id id;
    friend bool operator==(const DeleteFont&, const DeleteFont&) = default;
};

/// Replaces a font's definition, as long as every text that uses it stays valid.
struct ReplaceFont {
    Font font;
    friend bool operator==(const ReplaceFont&, const ReplaceFont&) = default;
};

struct CreateFontStack {
    FontStack stack;
    friend bool operator==(const CreateFontStack&, const CreateFontStack&) = default;
};

/// Removes a stack no text or typography refers to.
struct DeleteFontStack {
    Id id;
    friend bool operator==(const DeleteFontStack&, const DeleteFontStack&) = default;
};

/// Replaces a stack, as long as its first font still allows every text that refers to it.
struct ReplaceFontStack {
    FontStack stack;
    friend bool operator==(const ReplaceFontStack&, const ReplaceFontStack&) = default;
};

/// Replaces an instance's override of one component node. An empty patch removes it.
struct SetOverride {
    Id instance;
    Id node;
    NodePatch patch;
    friend bool operator==(const SetOverride&, const SetOverride&) = default;
};

struct SetComponentDefinition {
    Id component;
    ComponentDefinition definition;
    friend bool operator==(const SetComponentDefinition&, const SetComponentDefinition&) = default;
};

using OpBody = std::variant<CreateNode, DeleteNode, SetProps, MoveNode, CreateToken, DeleteToken, ReplaceToken,
                            CreateMode, DeleteMode, ReplaceMode, SetOverride, SetComponentDefinition, CreateFont,
                            DeleteFont, ReplaceFont, CreateFontStack, DeleteFontStack, ReplaceFontStack>;

/// An edit as it travels to the server: a client-generated id plus the change.
struct Op {
    Id id;
    OpBody body;
    friend bool operator==(const Op&, const Op&) = default;
};

} // namespace vc
