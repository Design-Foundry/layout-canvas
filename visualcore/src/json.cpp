#include "vc/json.hpp"

#include <array>
#include <nlohmann/json.hpp>

namespace vc {
namespace {

using nlohmann::json;

constexpr int kSnapshotVersion = 1;

constexpr std::array<std::pair<NodeType, std::string_view>, 4> kNodeTypes{{
    {NodeType::Frame, "frame"},
    {NodeType::Rect, "rect"},
    {NodeType::Ellipse, "ellipse"},
    {NodeType::Instance, "instance"},
}};

std::string_view typeName(NodeType type) {
    for (const auto& [value, name] : kNodeTypes) {
        if (value == type) return name;
    }
    return "rect";
}

std::optional<NodeType> typeFromName(std::string_view name) {
    for (const auto& [value, text] : kNodeTypes) {
        if (text == name) return value;
    }
    return std::nullopt;
}

json token(const TokenRef& ref) {
    return {{"token", ref.name}};
}

json write(const Value<Oklch>& value) {
    if (const auto* ref = std::get_if<TokenRef>(&value)) return token(*ref);
    const Oklch& c = std::get<Oklch>(value);
    return {{"l", c.l}, {"c", c.c}, {"h", c.h}, {"a", c.a}};
}

json write(const Value<double>& value) {
    if (const auto* ref = std::get_if<TokenRef>(&value)) return token(*ref);
    return std::get<double>(value);
}

void writePatch(json& out, const NodePatch& p) {
    if (p.name) out["name"] = *p.name;
    if (p.x) out["x"] = *p.x;
    if (p.y) out["y"] = *p.y;
    if (p.w) out["w"] = *p.w;
    if (p.h) out["h"] = *p.h;
    if (p.fill) out["fill"] = write(*p.fill);
    if (p.cornerRadius) out["cornerRadius"] = write(*p.cornerRadius);
    if (p.opacity) out["opacity"] = *p.opacity;
    if (p.visible) out["visible"] = *p.visible;
    if (p.clipContent) out["clipContent"] = *p.clipContent;
}

json write(const Node& n) {
    json out{{"id", n.id.toString()}, {"type", typeName(n.type)}, {"order", n.order}};
    if (!n.parent.isNil()) out["parent"] = n.parent.toString();
    writePatch(out, NodePatch{n.name, n.x, n.y, n.w, n.h, n.fill, n.cornerRadius, n.opacity, n.visible,
                              n.clipContent});
    return out;
}

// Readers return false when a present field has the wrong shape. Absent fields are fine.

std::optional<Id> readId(const json& j) {
    return j.is_string() ? Id::parse(j.get_ref<const std::string&>()) : std::nullopt;
}

std::optional<TokenRef> readToken(const json& j) {
    if (!j.is_object() || j.size() != 1) return std::nullopt;
    const auto it = j.find("token");
    if (it == j.end() || !it->is_string()) return std::nullopt;
    return TokenRef{it->get<std::string>()};
}

std::optional<Value<Oklch>> readColor(const json& j) {
    if (auto ref = readToken(j)) return *ref;
    if (!j.is_object()) return std::nullopt;
    Oklch c;
    const std::array<std::pair<const char*, double*>, 4> fields{{{"l", &c.l}, {"c", &c.c}, {"h", &c.h}, {"a", &c.a}}};
    for (const auto& [key, field] : fields) {
        const auto it = j.find(key);
        if (it == j.end() || !it->is_number()) return std::nullopt;
        *field = it->get<double>();
    }
    return c;
}

std::optional<Value<double>> readLength(const json& j) {
    if (j.is_number()) return j.get<double>();
    if (auto ref = readToken(j)) return *ref;
    return std::nullopt;
}

template <typename T, typename Reader>
bool readField(const json& j, const char* key, std::optional<T>& out, Reader reader) {
    const auto it = j.find(key);
    if (it == j.end()) return true;
    auto value = reader(*it);
    if (!value) return false;
    out = std::move(*value);
    return true;
}

std::optional<double> readNumber(const json& j) {
    return j.is_number() ? std::optional{j.get<double>()} : std::nullopt;
}

std::optional<bool> readBool(const json& j) {
    return j.is_boolean() ? std::optional{j.get<bool>()} : std::nullopt;
}

std::optional<std::string> readString(const json& j) {
    return j.is_string() ? std::optional{j.get<std::string>()} : std::nullopt;
}

std::optional<NodePatch> readPatch(const json& j) {
    NodePatch p;
    const bool ok = readField(j, "name", p.name, readString) && readField(j, "x", p.x, readNumber) &&
                    readField(j, "y", p.y, readNumber) && readField(j, "w", p.w, readNumber) &&
                    readField(j, "h", p.h, readNumber) && readField(j, "fill", p.fill, readColor) &&
                    readField(j, "cornerRadius", p.cornerRadius, readLength) &&
                    readField(j, "opacity", p.opacity, readNumber) &&
                    readField(j, "visible", p.visible, readBool) &&
                    readField(j, "clipContent", p.clipContent, readBool);
    return ok ? std::optional{p} : std::nullopt;
}

std::optional<Node> readNode(const json& j) {
    if (!j.is_object()) return std::nullopt;
    Node n;

    const auto id = j.contains("id") ? readId(j["id"]) : std::nullopt;
    const auto type = j.contains("type") && j["type"].is_string()
                          ? typeFromName(j["type"].get_ref<const std::string&>())
                          : std::nullopt;
    const auto order = j.contains("order") ? readString(j["order"]) : std::nullopt;
    if (!id || !type || !order) return std::nullopt;
    n.id = *id;
    n.type = *type;
    n.order = *order;

    if (j.contains("parent")) {
        const auto parent = readId(j["parent"]);
        if (!parent) return std::nullopt;
        n.parent = *parent;
    }

    const auto patch = readPatch(j);
    if (!patch) return std::nullopt;
    applyPatch(n, *patch);
    return n;
}

std::optional<Id> readIdField(const json& j, const char* key) {
    return j.contains(key) ? readId(j[key]) : std::nullopt;
}

std::optional<OpBody> readOp(std::string_view kind, const json& j) {
    if (!j.is_object()) return std::nullopt;

    if (kind == "CreateNode") {
        if (!j.contains("node")) return std::nullopt;
        auto node = readNode(j["node"]);
        if (!node) return std::nullopt;
        return CreateNode{std::move(*node)};
    }
    const auto id = readIdField(j, "id");
    if (!id) return std::nullopt;

    if (kind == "DeleteNode") {
        return DeleteNode{*id};
    }
    if (kind == "SetProps") {
        auto patch = j.contains("patch") && j["patch"].is_object() ? readPatch(j["patch"]) : std::nullopt;
        if (!patch) return std::nullopt;
        return SetProps{*id, std::move(*patch)};
    }
    if (kind == "MoveNode") {
        const auto order = j.contains("order") ? readString(j["order"]) : std::nullopt;
        if (!order) return std::nullopt;
        Id parent;
        if (j.contains("parent")) {
            const auto p = readId(j["parent"]);
            if (!p) return std::nullopt;
            parent = *p;
        }
        return MoveNode{*id, parent, *order};
    }
    return std::nullopt;
}

json writeOp(const OpBody& op) {
    return std::visit(
        [](const auto& body) -> json {
            using T = std::decay_t<decltype(body)>;
            if constexpr (std::is_same_v<T, CreateNode>) {
                return {{"node", write(body.node)}};
            } else if constexpr (std::is_same_v<T, DeleteNode>) {
                return {{"id", body.id.toString()}};
            } else if constexpr (std::is_same_v<T, SetProps>) {
                json patch = json::object();
                writePatch(patch, body.patch);
                return {{"id", body.id.toString()}, {"patch", patch}};
            } else {
                json move{{"id", body.id.toString()}, {"order", body.order}};
                if (!body.parent.isNil()) move["parent"] = body.parent.toString();
                return move;
            }
        },
        op);
}

void writeLayers(const Document& document, const Id& parent, int depth, json& rows) {
    const auto& children = document.children(parent);
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        const Node& node = *document.find(*it);
        rows.push_back({{"id", node.id.toString()}, {"name", node.name}, {"type", typeName(node.type)},
                        {"depth", depth}, {"visible", node.visible}});
        writeLayers(document, node.id, depth + 1, rows);
    }
}

} // namespace

std::optional<OpBody> opFromJson(std::string_view kind, std::string_view data) {
    return readOp(kind, json::parse(data, nullptr, false));
}

std::string_view opKind(const OpBody& op) {
    constexpr std::array<std::string_view, 4> kKinds{"CreateNode", "DeleteNode", "SetProps", "MoveNode"};
    return kKinds[op.index()];
}

std::string opToJson(const OpBody& op) {
    return writeOp(op).dump();
}

std::string opsToJson(const std::vector<Op>& ops) {
    json out = json::array();
    for (const Op& op : ops) {
        out.push_back({{"id", op.id.toString()}, {"kind", opKind(op.body)}, {"data", writeOp(op.body)}});
    }
    return out.dump();
}

std::optional<std::vector<OpBody>> opsFromJson(std::string_view data) {
    const json j = json::parse(data, nullptr, false);
    if (!j.is_array()) return std::nullopt;
    std::vector<OpBody> ops;
    ops.reserve(j.size());
    for (const json& item : j) {
        if (!item.is_object() || !item.contains("kind") || !item["kind"].is_string() || !item.contains("data")) {
            return std::nullopt;
        }
        auto op = readOp(item["kind"].get_ref<const std::string&>(), item["data"]);
        if (!op) return std::nullopt;
        ops.push_back(std::move(*op));
    }
    return ops;
}

std::string inspectNodeJson(const Node& node) {
    json out = write(node);
    if (const auto* color = std::get_if<Oklch>(&node.fill)) {
        out["fill"]["hex"] = toHex(*color);
    }
    return out.dump();
}

std::optional<NodePatch> patchFromJson(std::string_view data) {
    const json j = json::parse(data, nullptr, false);
    return j.is_object() ? readPatch(j) : std::nullopt;
}

std::string colorToJson(const Oklch& color) {
    return write(Value<Oklch>{color}).dump();
}

std::string layersToJson(const Document& document) {
    json rows = json::array();
    writeLayers(document, Id{}, 0, rows);
    return rows.dump();
}

std::string snapshotToJson(const Document& document) {
    json nodes = json::array();
    document.forEachInPaintOrder([&](const Node& node) { nodes.push_back(write(node)); });
    return json{{"version", kSnapshotVersion}, {"nodes", nodes}}.dump();
}

std::optional<std::vector<CreateNode>> snapshotFromJson(std::string_view data) {
    const json j = json::parse(data, nullptr, false);
    if (!j.is_object() || !j.contains("version") || j["version"] != kSnapshotVersion ||
        !j.contains("nodes") || !j["nodes"].is_array()) {
        return std::nullopt;
    }
    std::vector<CreateNode> ops;
    ops.reserve(j["nodes"].size());
    for (const json& item : j["nodes"]) {
        auto node = readNode(item);
        if (!node) return std::nullopt;
        ops.push_back({std::move(*node)});
    }
    return ops;
}

} // namespace vc
