#include "vc/json.hpp"

#include <algorithm>
#include <array>
#include <unordered_set>
#include <nlohmann/json.hpp>

namespace vc {
namespace {

using nlohmann::json;

constexpr int kSnapshotVersion = 2;

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

template <typename E, std::size_t N>
std::string_view nameOf(const std::array<std::pair<E, std::string_view>, N>& names, E value) {
    for (const auto& [e, name] : names) {
        if (e == value) return name;
    }
    return names[0].second;
}

template <typename E, std::size_t N>
std::optional<E> valueOf(const std::array<std::pair<E, std::string_view>, N>& names, const json& j) {
    if (!j.is_string()) return std::nullopt;
    for (const auto& [e, name] : names) {
        if (name == j.get_ref<const std::string&>()) return e;
    }
    return std::nullopt;
}

constexpr std::array<std::pair<ColorChannel, std::string_view>, 4> kChannels{{
    {ColorChannel::L, "l"}, {ColorChannel::C, "c"}, {ColorChannel::H, "h"}, {ColorChannel::Alpha, "alpha"}}};
constexpr std::array<std::pair<ColorOp, std::string_view>, 3> kColorOps{{
    {ColorOp::Set, "set"}, {ColorOp::Delta, "delta"}, {ColorOp::Factor, "factor"}}};
constexpr std::array<std::pair<MagnitudeOp, std::string_view>, 4> kMagnitudeOps{{
    {MagnitudeOp::Factor, "factor"}, {MagnitudeOp::Delta, "delta"}, {MagnitudeOp::Min, "min"}, {MagnitudeOp::Max, "max"}}};
constexpr std::array<std::pair<StrokeStyle, std::string_view>, 5> kStrokeStyles{{
    {StrokeStyle::Solid, "solid"}, {StrokeStyle::None, "none"}, {StrokeStyle::Dashed, "dashed"},
    {StrokeStyle::Dotted, "dotted"}, {StrokeStyle::Double, "double"}}};
constexpr std::array<std::pair<StrokeAlign, std::string_view>, 3> kStrokeAligns{{
    {StrokeAlign::Inside, "inside"}, {StrokeAlign::Center, "center"}, {StrokeAlign::Outside, "outside"}}};
constexpr std::array<std::pair<LayoutType, std::string_view>, 3> kLayoutTypes{{
    {LayoutType::None, "none"}, {LayoutType::Row, "row"}, {LayoutType::Column, "column"}}};
constexpr std::array<std::pair<Justify, std::string_view>, 6> kJustifies{{
    {Justify::Start, "start"}, {Justify::Center, "center"}, {Justify::End, "end"},
    {Justify::SpaceBetween, "spaceBetween"}, {Justify::SpaceAround, "spaceAround"},
    {Justify::SpaceEvenly, "spaceEvenly"}}};
constexpr std::array<std::pair<Align, std::string_view>, 4> kAligns{{
    {Align::Start, "start"}, {Align::Center, "center"}, {Align::End, "end"}, {Align::Stretch, "stretch"}}};
constexpr std::array<std::pair<Sizing, std::string_view>, 3> kSizings{{
    {Sizing::Fixed, "fixed"}, {Sizing::Hug, "hug"}, {Sizing::Fill, "fill"}}};

json token(const TokenRef& ref) {
    return {{"token", ref.token.toString()}};
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

json write(const std::optional<Value<double>>& value) {
    return value ? write(*value) : json(nullptr);
}

template <typename T>
json write(const std::array<std::optional<Value<T>>, 4>& values) {
    json out = json::array();
    for (const auto& value : values) out.push_back(write(value));
    return out;
}

json write(const Value<Stroke>& value) {
    if (const auto* ref = std::get_if<TokenRef>(&value)) return token(*ref);
    const Stroke& stroke = std::get<Stroke>(value);
    json out{{"width", write(stroke.width)}, {"style", nameOf(kStrokeStyles, stroke.style)},
             {"color", write(stroke.color)}, {"align", nameOf(kStrokeAligns, stroke.align)}};
    if (std::any_of(stroke.sides.begin(), stroke.sides.end(), [](const auto& side) { return side.has_value(); })) {
        out["sides"] = write(stroke.sides);
    }
    return out;
}

json write(const Value<std::vector<Shadow>>& value) {
    if (const auto* ref = std::get_if<TokenRef>(&value)) return token(*ref);
    json out = json::array();
    for (const Shadow& layer : std::get<std::vector<Shadow>>(value)) {
        out.push_back({{"x", layer.x}, {"y", layer.y}, {"blur", layer.blur}, {"spread", layer.spread},
                       {"color", write(layer.color)}, {"inset", layer.inset}, {"visible", layer.visible}});
    }
    return out;
}

json write(const Layout& layout) {
    json padding = json::array();
    for (const auto& side : layout.padding) padding.push_back(write(side));
    return {{"type", nameOf(kLayoutTypes, layout.type)}, {"gap", write(layout.gap)}, {"padding", padding},
            {"justify", nameOf(kJustifies, layout.justify)}, {"align", nameOf(kAligns, layout.align)}};
}

json write(const SizeLimits& limits) {
    json out = json::object();
    if (limits.minWidth) out["minWidth"] = write(*limits.minWidth);
    if (limits.maxWidth) out["maxWidth"] = write(*limits.maxWidth);
    if (limits.minHeight) out["minHeight"] = write(*limits.minHeight);
    if (limits.maxHeight) out["maxHeight"] = write(*limits.maxHeight);
    return out;
}

void writePatch(json& out, const NodePatch& p) {
    if (p.name) out["name"] = *p.name;
    if (p.x) out["x"] = *p.x;
    if (p.y) out["y"] = *p.y;
    if (p.w) out["w"] = *p.w;
    if (p.h) out["h"] = *p.h;
    if (p.fill) out["fill"] = write(*p.fill);
    if (p.fillVisible) out["fillVisible"] = *p.fillVisible;
    if (p.cornerRadius) out["cornerRadius"] = write(*p.cornerRadius);
    if (p.cornerRadii) out["cornerRadii"] = write(*p.cornerRadii);
    if (p.stroke) out["stroke"] = write(*p.stroke);
    if (p.shadows) out["shadows"] = write(*p.shadows);
    if (p.opacity) out["opacity"] = write(*p.opacity);
    if (p.visible) out["visible"] = *p.visible;
    if (p.locked) out["locked"] = *p.locked;
    if (p.clipContent) out["clipContent"] = *p.clipContent;
    if (p.layout) out["layout"] = write(*p.layout);
    if (p.widthSizing) out["widthSizing"] = nameOf(kSizings, *p.widthSizing);
    if (p.heightSizing) out["heightSizing"] = nameOf(kSizings, *p.heightSizing);
    if (p.limits) out["limits"] = write(*p.limits);
    if (p.absolute) out["absolute"] = *p.absolute;
}

/// The whole node, leaving out properties that still have their default value.
json write(const Node& n) {
    json out{{"id", n.id.toString()}, {"type", typeName(n.type)}, {"order", n.order}};
    if (!n.parent.isNil()) out["parent"] = n.parent.toString();
    NodePatch p = fullPatch(n);
    const Node defaults;
    if (n.fillVisible == defaults.fillVisible) p.fillVisible.reset();
    if (n.cornerRadii == defaults.cornerRadii) p.cornerRadii.reset();
    if (n.stroke == defaults.stroke) p.stroke.reset();
    if (n.shadows == defaults.shadows) p.shadows.reset();
    if (n.locked == defaults.locked) p.locked.reset();
    if (n.layout == defaults.layout) p.layout.reset();
    if (n.widthSizing == defaults.widthSizing) p.widthSizing.reset();
    if (n.heightSizing == defaults.heightSizing) p.heightSizing.reset();
    if (n.limits == defaults.limits) p.limits.reset();
    if (n.absolute == defaults.absolute) p.absolute.reset();
    writePatch(out, p);
    return out;
}

// Readers return false when a present field has the wrong shape. Absent fields are fine.

std::optional<Id> readId(const json& j) {
    return j.is_string() ? Id::parse(j.get_ref<const std::string&>()) : std::nullopt;
}

std::optional<TokenRef> readToken(const json& j) {
    if (!j.is_object() || j.size() != 1) return std::nullopt;
    const auto it = j.find("token");
    if (it == j.end()) return std::nullopt;
    const auto id = readId(*it);
    return id ? std::optional{TokenRef{*id}} : std::nullopt;
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

std::optional<std::array<std::optional<Value<double>>, 4>> readFour(const json& j) {
    if (!j.is_array() || j.size() != 4) return std::nullopt;
    std::array<std::optional<Value<double>>, 4> out{};
    for (std::size_t i = 0; i < 4; ++i) {
        if (j[i].is_null()) continue;
        out[i] = readLength(j[i]);
        if (!out[i]) return std::nullopt;
    }
    return out;
}

std::optional<Value<Stroke>> readStroke(const json& j) {
    if (auto ref = readToken(j)) return *ref;
    if (!j.is_object()) return std::nullopt;
    Stroke stroke;
    std::optional<Value<double>> width;
    std::optional<Value<Oklch>> color;
    if (!readField(j, "width", width, readLength) || !readField(j, "color", color, readColor)) return std::nullopt;
    if (width) stroke.width = *width;
    if (color) stroke.color = *color;
    if (const auto it = j.find("style"); it != j.end()) {
        const auto style = valueOf(kStrokeStyles, *it);
        if (!style) return std::nullopt;
        stroke.style = *style;
    }
    if (const auto it = j.find("align"); it != j.end()) {
        const auto align = valueOf(kStrokeAligns, *it);
        if (!align) return std::nullopt;
        stroke.align = *align;
    }
    if (const auto it = j.find("sides"); it != j.end()) {
        const auto sides = readFour(*it);
        if (!sides) return std::nullopt;
        stroke.sides = *sides;
    }
    return stroke;
}

std::optional<Value<std::vector<Shadow>>> readShadows(const json& j) {
    if (auto ref = readToken(j)) return *ref;
    if (!j.is_array()) return std::nullopt;
    std::vector<Shadow> layers;
    for (const json& item : j) {
        if (!item.is_object()) return std::nullopt;
        Shadow layer;
        const std::array<std::pair<const char*, double*>, 4> numbers{
            {{"x", &layer.x}, {"y", &layer.y}, {"blur", &layer.blur}, {"spread", &layer.spread}}};
        for (const auto& [key, field] : numbers) {
            if (const auto it = item.find(key); it != item.end()) {
                if (!it->is_number()) return std::nullopt;
                *field = it->get<double>();
            }
        }
        std::optional<Value<Oklch>> color;
        std::optional<bool> inset;
        std::optional<bool> visible;
        if (!readField(item, "color", color, readColor) || !readField(item, "inset", inset, readBool) ||
            !readField(item, "visible", visible, readBool)) {
            return std::nullopt;
        }
        if (color) layer.color = *color;
        layer.inset = inset.value_or(false);
        layer.visible = visible.value_or(true);
        layers.push_back(std::move(layer));
    }
    return layers;
}

std::optional<Layout> readLayout(const json& j) {
    if (!j.is_object()) return std::nullopt;
    Layout layout;
    if (const auto it = j.find("type"); it != j.end()) {
        const auto type = valueOf(kLayoutTypes, *it);
        if (!type) return std::nullopt;
        layout.type = *type;
    }
    if (const auto it = j.find("justify"); it != j.end()) {
        const auto justify = valueOf(kJustifies, *it);
        if (!justify) return std::nullopt;
        layout.justify = *justify;
    }
    if (const auto it = j.find("align"); it != j.end()) {
        const auto align = valueOf(kAligns, *it);
        if (!align) return std::nullopt;
        layout.align = *align;
    }
    std::optional<Value<double>> gap;
    if (!readField(j, "gap", gap, readLength)) return std::nullopt;
    if (gap) layout.gap = *gap;
    if (const auto it = j.find("padding"); it != j.end()) {
        const auto padding = readFour(*it);
        if (!padding) return std::nullopt;
        for (std::size_t i = 0; i < 4; ++i) layout.padding[i] = (*padding)[i].value_or(0.0);
    }
    return layout;
}

std::optional<SizeLimits> readLimits(const json& j) {
    if (!j.is_object()) return std::nullopt;
    SizeLimits limits;
    const bool ok = readField(j, "minWidth", limits.minWidth, readLength) &&
                    readField(j, "maxWidth", limits.maxWidth, readLength) &&
                    readField(j, "minHeight", limits.minHeight, readLength) &&
                    readField(j, "maxHeight", limits.maxHeight, readLength);
    return ok ? std::optional{limits} : std::nullopt;
}

std::optional<Sizing> readSizing(const json& j) {
    return valueOf(kSizings, j);
}

std::optional<NodePatch> readPatch(const json& j) {
    NodePatch p;
    const bool ok = readField(j, "name", p.name, readString) && readField(j, "x", p.x, readNumber) &&
                    readField(j, "y", p.y, readNumber) && readField(j, "w", p.w, readNumber) &&
                    readField(j, "h", p.h, readNumber) && readField(j, "fill", p.fill, readColor) &&
                    readField(j, "fillVisible", p.fillVisible, readBool) &&
                    readField(j, "cornerRadius", p.cornerRadius, readLength) &&
                    readField(j, "cornerRadii", p.cornerRadii, readFour) &&
                    readField(j, "stroke", p.stroke, readStroke) &&
                    readField(j, "shadows", p.shadows, readShadows) &&
                    readField(j, "opacity", p.opacity, readLength) &&
                    readField(j, "visible", p.visible, readBool) &&
                    readField(j, "locked", p.locked, readBool) &&
                    readField(j, "clipContent", p.clipContent, readBool) &&
                    readField(j, "layout", p.layout, readLayout) &&
                    readField(j, "widthSizing", p.widthSizing, readSizing) &&
                    readField(j, "heightSizing", p.heightSizing, readSizing) &&
                    readField(j, "limits", p.limits, readLimits) &&
                    readField(j, "absolute", p.absolute, readBool);
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

json writeModifier(const Modifier& modifier);

json writeLiteral(const TokenLiteral& literal) {
    if (const auto* color = std::get_if<Oklch>(&literal)) {
        return {{"l", color->l}, {"c", color->c}, {"h", color->h}};
    }
    if (const auto* border = std::get_if<BorderValue>(&literal)) {
        return {{"width", border->width.toString()}, {"style", nameOf(kStrokeStyles, border->style)},
                {"color", border->color.toString()}};
    }
    if (const auto* shadow = std::get_if<ShadowValue>(&literal)) {
        json layers = json::array();
        for (const ShadowLayerValue& layer : shadow->layers) {
            json item{{"offsetX", layer.x}, {"offsetY", layer.y}, {"blur", layer.blur}, {"spread", layer.spread},
                      {"inset", layer.inset}, {"color", layer.color.toString()}};
            if (!layer.colorModifiers.empty()) {
                json modifiers = json::array();
                for (const Modifier& modifier : layer.colorModifiers) modifiers.push_back(writeModifier(modifier));
                item["colorModifiers"] = modifiers;
            }
            layers.push_back(item);
        }
        return {{"layers", layers}};
    }
    const auto& magnitude = std::get<Magnitude>(literal);
    json out{{"value", magnitude.value}};
    if (magnitude.unit != Unit::None) out["unit"] = unitName(magnitude.unit);
    return out;
}

void writeOperand(json& out, const std::optional<double>& value, const std::optional<Id>& ref) {
    if (value) out["value"] = *value;
    if (ref) out["ref"] = ref->toString();
}

json writeModifier(const Modifier& modifier) {
    if (const auto* channel = std::get_if<ColorChannelModifier>(&modifier)) {
        json out{{"channel", nameOf(kChannels, channel->channel)}, {"kind", nameOf(kColorOps, channel->op)}};
        writeOperand(out, channel->value, channel->ref);
        return out;
    }
    if (const auto* mixer = std::get_if<ColorMixModifier>(&modifier)) {
        return {{"mix", mixer->token.toString()}, {"amount", mixer->amount}};
    }
    const auto& step = std::get<MagnitudeModifier>(modifier);
    json out{{"kind", nameOf(kMagnitudeOps, step.op)}};
    writeOperand(out, step.value, step.ref);
    return out;
}

json writeTokenDef(const Token& token) {
    json out{{"id", token.id.toString()}, {"type", tokenTypeName(token.type)}, {"identifier", token.identifier},
             {"order", token.order}};
    if (!token.displayName.empty()) out["displayName"] = token.displayName;
    if (!token.description.empty()) out["description"] = token.description;
    if (token.deprecated) out["deprecated"] = true;
    if (token.value) out["value"] = writeLiteral(*token.value);
    if (token.isAlias()) {
        json source = json::object();
        for (const auto& [mode, target] : token.source) source[mode.toString()] = target.toString();
        out["source"] = source;
    }
    if (!token.modifiers.empty()) {
        json modifiers = json::array();
        for (const Modifier& modifier : token.modifiers) modifiers.push_back(writeModifier(modifier));
        out["modifiers"] = modifiers;
    }
    return out;
}

json writeMode(const Mode& mode) {
    return {{"id", mode.id.toString()}, {"name", mode.name}, {"order", mode.order}};
}

std::optional<Modifier> readModifier(const json& j);

std::optional<TokenLiteral> readLiteral(TokenType type, const json& j) {
    if (!j.is_object()) return std::nullopt;
    if (type == TokenType::Border) {
        const auto width = j.contains("width") ? readId(j["width"]) : std::nullopt;
        const auto color = j.contains("color") ? readId(j["color"]) : std::nullopt;
        const auto style = j.contains("style") ? valueOf(kStrokeStyles, j["style"]) : std::optional{StrokeStyle::Solid};
        if (!width || !color || !style) return std::nullopt;
        return BorderValue{*width, *style, *color};
    }
    if (type == TokenType::Shadow) {
        ShadowValue shadow;
        if (!j.contains("layers") || !j["layers"].is_array()) return std::nullopt;
        for (const json& item : j["layers"]) {
            if (!item.is_object()) return std::nullopt;
            ShadowLayerValue layer;
            const std::array<std::pair<const char*, double*>, 4> numbers{
                {{"offsetX", &layer.x}, {"offsetY", &layer.y}, {"blur", &layer.blur}, {"spread", &layer.spread}}};
            for (const auto& [key, field] : numbers) {
                if (const auto it = item.find(key); it != item.end()) {
                    if (!it->is_number()) return std::nullopt;
                    *field = it->get<double>();
                }
            }
            const auto color = item.contains("color") ? readId(item["color"]) : std::nullopt;
            if (!color) return std::nullopt;
            layer.color = *color;
            if (const auto it = item.find("inset"); it != item.end()) {
                if (!it->is_boolean()) return std::nullopt;
                layer.inset = it->get<bool>();
            }
            if (const auto it = item.find("colorModifiers"); it != item.end()) {
                if (!it->is_array()) return std::nullopt;
                for (const json& m : *it) {
                    auto modifier = readModifier(m);
                    if (!modifier) return std::nullopt;
                    layer.colorModifiers.push_back(std::move(*modifier));
                }
            }
            shadow.layers.push_back(std::move(layer));
        }
        return shadow;
    }
    if (type == TokenType::Color) {
        Oklch color;
        const std::array<std::pair<const char*, double*>, 3> fields{{{"l", &color.l}, {"c", &color.c}, {"h", &color.h}}};
        for (const auto& [key, field] : fields) {
            const auto it = j.find(key);
            if (it == j.end() || !it->is_number()) return std::nullopt;
            *field = it->get<double>();
        }
        return color;
    }
    const auto value = j.find("value");
    if (value == j.end() || !value->is_number()) return std::nullopt;
    Magnitude magnitude{value->get<double>(), Unit::None};
    if (const auto unit = j.find("unit"); unit != j.end()) {
        const auto parsed = unit->is_string() ? unitFromName(unit->get_ref<const std::string&>()) : std::nullopt;
        if (!parsed) return std::nullopt;
        magnitude.unit = *parsed;
    }
    return magnitude;
}

bool readOperand(const json& j, std::optional<double>& value, std::optional<Id>& ref) {
    if (const auto it = j.find("value"); it != j.end()) {
        if (!it->is_number()) return false;
        value = it->get<double>();
    }
    if (const auto it = j.find("ref"); it != j.end()) {
        ref = readId(*it);
        if (!ref) return false;
    }
    return true;
}

std::optional<Modifier> readModifier(const json& j) {
    if (!j.is_object()) return std::nullopt;
    if (j.contains("mix")) {
        const auto token = readId(j["mix"]);
        const auto amount = j.find("amount");
        if (!token || amount == j.end() || !amount->is_number()) return std::nullopt;
        return ColorMixModifier{*token, amount->get<double>()};
    }
    if (j.contains("channel")) {
        ColorChannelModifier channel;
        const auto name = valueOf(kChannels, j["channel"]);
        const auto op = j.contains("kind") ? valueOf(kColorOps, j["kind"]) : std::nullopt;
        if (!name || !op || !readOperand(j, channel.value, channel.ref)) return std::nullopt;
        channel.channel = *name;
        channel.op = *op;
        return channel;
    }
    MagnitudeModifier step;
    const auto op = j.contains("kind") ? valueOf(kMagnitudeOps, j["kind"]) : std::nullopt;
    if (!op || !readOperand(j, step.value, step.ref)) return std::nullopt;
    step.op = *op;
    return step;
}

std::optional<Token> readTokenDef(const json& j) {
    if (!j.is_object()) return std::nullopt;
    Token token;
    const auto id = j.contains("id") ? readId(j["id"]) : std::nullopt;
    const auto type = j.contains("type") && j["type"].is_string()
                          ? tokenTypeFromName(j["type"].get_ref<const std::string&>())
                          : std::nullopt;
    if (!id || !type) return std::nullopt;
    token.id = *id;
    token.type = *type;

    const std::array<std::pair<const char*, std::string*>, 4> texts{{{"identifier", &token.identifier},
        {"order", &token.order}, {"displayName", &token.displayName}, {"description", &token.description}}};
    for (const auto& [key, field] : texts) {
        if (const auto it = j.find(key); it != j.end()) {
            if (!it->is_string()) return std::nullopt;
            *field = it->get<std::string>();
        }
    }
    if (const auto it = j.find("deprecated"); it != j.end()) {
        if (!it->is_boolean()) return std::nullopt;
        token.deprecated = it->get<bool>();
    }
    if (const auto it = j.find("value"); it != j.end()) {
        token.value = readLiteral(token.type, *it);
        if (!token.value) return std::nullopt;
    }
    if (const auto it = j.find("source"); it != j.end()) {
        if (!it->is_object()) return std::nullopt;
        for (const auto& [mode, target] : it->items()) {
            const auto modeId = Id::parse(mode);
            const auto targetId = readId(target);
            if (!modeId || !targetId) return std::nullopt;
            token.source[*modeId] = *targetId;
        }
    }
    if (const auto it = j.find("modifiers"); it != j.end()) {
        if (!it->is_array()) return std::nullopt;
        for (const json& item : *it) {
            auto modifier = readModifier(item);
            if (!modifier) return std::nullopt;
            token.modifiers.push_back(std::move(*modifier));
        }
    }
    return token;
}

std::optional<Mode> readMode(const json& j) {
    if (!j.is_object()) return std::nullopt;
    const auto id = j.contains("id") ? readId(j["id"]) : std::nullopt;
    const auto name = j.contains("name") ? readString(j["name"]) : std::nullopt;
    const auto order = j.contains("order") ? readString(j["order"]) : std::optional<std::string>{""};
    if (!id || !name || !order) return std::nullopt;
    return Mode{*id, *name, *order};
}

std::optional<OpBody> readOp(std::string_view kind, const json& j) {
    if (!j.is_object()) return std::nullopt;

    if (kind == "CreateToken" || kind == "ReplaceToken") {
        auto token = j.contains("token") ? readTokenDef(j["token"]) : std::nullopt;
        if (!token) return std::nullopt;
        if (kind == "CreateToken") return CreateToken{std::move(*token)};
        return ReplaceToken{std::move(*token)};
    }
    if (kind == "CreateMode" || kind == "ReplaceMode") {
        auto mode = j.contains("mode") ? readMode(j["mode"]) : std::nullopt;
        if (!mode) return std::nullopt;
        if (kind == "CreateMode") return CreateMode{std::move(*mode)};
        return ReplaceMode{std::move(*mode)};
    }

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
    if (kind == "DeleteToken") {
        return DeleteToken{*id};
    }
    if (kind == "DeleteMode") {
        return DeleteMode{*id};
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
            } else if constexpr (std::is_same_v<T, MoveNode>) {
                json move{{"id", body.id.toString()}, {"order", body.order}};
                if (!body.parent.isNil()) move["parent"] = body.parent.toString();
                return move;
            } else if constexpr (std::is_same_v<T, CreateToken> || std::is_same_v<T, ReplaceToken>) {
                return {{"token", writeTokenDef(body.token)}};
            } else if constexpr (std::is_same_v<T, CreateMode> || std::is_same_v<T, ReplaceMode>) {
                return {{"mode", writeMode(body.mode)}};
            } else {
                return {{"id", body.id.toString()}};
            }
        },
        op);
}

void writeLayers(const Document& document, const Id& parent, int depth, json& rows) {
    const auto& children = document.children(parent);
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        const Node& node = *document.find(*it);
        rows.push_back({{"id", node.id.toString()}, {"name", node.name}, {"type", typeName(node.type)},
                        {"depth", depth}, {"visible", node.visible}, {"locked", node.locked}});
        writeLayers(document, node.id, depth + 1, rows);
    }
}

} // namespace

std::optional<OpBody> opFromJson(std::string_view kind, std::string_view data) {
    return readOp(kind, json::parse(data, nullptr, false));
}

std::string_view opKind(const OpBody& op) {
    constexpr std::array<std::string_view, 10> kKinds{"CreateNode",  "DeleteNode",  "SetProps",
                                                      "MoveNode",    "CreateToken", "DeleteToken",
                                                      "ReplaceToken", "CreateMode", "DeleteMode",
                                                      "ReplaceMode"};
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

std::string inspectNodeJson(const Document& document, const Node& node, const Id& mode) {
    json out{{"id", node.id.toString()}, {"type", typeName(node.type)}, {"order", node.order}};
    if (!node.parent.isNil()) out["parent"] = node.parent.toString();
    writePatch(out, fullPatch(node));
    if (const auto* color = std::get_if<Oklch>(&node.fill)) {
        out["fill"]["hex"] = toHex(*color);
    }
    const Rect box = document.box(node.id, mode);
    out["box"] = {{"x", box.x}, {"y", box.y}, {"w", box.w}, {"h", box.h}};
    const Node* parent = document.find(node.parent);
    out["inFlow"] = parent && parent->layout.type != LayoutType::None && !node.absolute;
    out["parentLayout"] = parent ? nameOf(kLayoutTypes, parent->layout.type) : "none";
    return out.dump();
}

std::optional<NodePatch> patchFromJson(std::string_view data) {
    const json j = json::parse(data, nullptr, false);
    return j.is_object() ? readPatch(j) : std::nullopt;
}

std::string colorToJson(const Oklch& color) {
    return write(Value<Oklch>{color}).dump();
}

std::string frameLabelsToJson(const Document& document, const Camera& camera, const Id& mode) {
    json labels = json::array();
    for (const Id& id : document.children(Id{})) {
        const Node& node = *document.find(id);
        if (node.type != NodeType::Frame || !node.visible) continue;
        const Rect box = document.box(id, mode);
        const Vec2 topLeft = camera.toScreen({box.x, box.y});
        labels.push_back({{"id", id.toString()}, {"name", node.name}, {"x", topLeft.x}, {"y", topLeft.y},
                          {"w", box.w * camera.zoom()}});
    }
    return labels.dump();
}

std::string layersToJson(const Document& document) {
    json rows = json::array();
    writeLayers(document, Id{}, 0, rows);
    return rows.dump();
}

std::string snapshotToJson(const Document& document) {
    json modes = json::array();
    for (const Mode* mode : document.modes()) modes.push_back(writeMode(*mode));

    json tokens = json::array();
    std::unordered_set<Id> written;
    const auto writeWithDependencies = [&](const Token& token, auto&& self) -> void {
        if (!written.insert(token.id).second) return;
        forEachTokenReference(token, [&](const Id& dependency) {
            if (const Token* other = document.findToken(dependency)) self(*other, self);
        });
        tokens.push_back(writeTokenDef(token));
    };
    for (const Token* token : document.tokens()) writeWithDependencies(*token, writeWithDependencies);

    json nodes = json::array();
    document.forEachInPaintOrder([&](const Node& node) { nodes.push_back(write(node)); });
    return json{{"version", kSnapshotVersion}, {"modes", modes}, {"tokens", tokens}, {"nodes", nodes}}.dump();
}

std::optional<std::vector<OpBody>> snapshotFromJson(std::string_view data) {
    const json j = json::parse(data, nullptr, false);
    if (!j.is_object() || !j.contains("version") || !j["version"].is_number_integer()) return std::nullopt;
    const int version = j["version"].get<int>();
    if (version < 1 || version > kSnapshotVersion || !j.contains("nodes") || !j["nodes"].is_array()) {
        return std::nullopt;
    }

    std::vector<OpBody> ops;
    if (version >= 2) {
        if (!j.contains("modes") || !j["modes"].is_array() || !j.contains("tokens") || !j["tokens"].is_array()) {
            return std::nullopt;
        }
        for (const json& item : j["modes"]) {
            auto mode = readMode(item);
            if (!mode) return std::nullopt;
            if (mode->id == kDefaultModeId) {
                ops.push_back(ReplaceMode{std::move(*mode)});
            } else {
                ops.push_back(CreateMode{std::move(*mode)});
            }
        }
        for (const json& item : j["tokens"]) {
            auto token = readTokenDef(item);
            if (!token) return std::nullopt;
            ops.push_back(CreateToken{std::move(*token)});
        }
    }
    for (const json& item : j["nodes"]) {
        auto node = readNode(item);
        if (!node) return std::nullopt;
        ops.push_back(CreateNode{std::move(*node)});
    }
    return ops;
}

std::string tokensToJson(const Document& document, const Id& mode) {
    json rows = json::array();
    const auto color = [](const Oklch& c) {
        return json{{"l", c.l}, {"c", c.c}, {"h", c.h}, {"a", c.a}, {"hex", toHex(c)}};
    };
    for (const Token* token : document.tokens()) {
        json row = writeTokenDef(*token);
        if (token->type == TokenType::Border) {
            if (const auto stroke = document.resolveStroke(TokenRef{token->id}, mode)) {
                row["resolved"] = {{"width", stroke->widths[kTop]}, {"style", nameOf(kStrokeStyles, stroke->style)},
                                   {"color", color(stroke->color)}};
            }
        } else if (token->type == TokenType::Shadow) {
            json layers = json::array();
            for (const ResolvedShadow& layer : document.resolveShadows(TokenRef{token->id}, mode)) {
                layers.push_back({{"x", layer.x}, {"y", layer.y}, {"blur", layer.blur}, {"spread", layer.spread},
                                  {"inset", layer.inset}, {"color", color(layer.color)}});
            }
            row["resolved"] = {{"layers", layers}};
        } else if (const auto resolved = document.resolveToken(token->id, mode)) {
            json value = writeLiteral(*resolved);
            if (const auto* color = std::get_if<Oklch>(&*resolved)) {
                value["a"] = color->a;
                value["hex"] = toHex(*color);
            }
            row["resolved"] = value;
        }
        row["referenced"] = document.isTokenReferenced(token->id);
        rows.push_back(row);
    }
    return rows.dump();
}

std::string modesToJson(const Document& document) {
    json rows = json::array();
    for (const Mode* mode : document.modes()) {
        json row = writeMode(*mode);
        row["isDefault"] = mode->id == kDefaultModeId;
        rows.push_back(row);
    }
    return rows.dump();
}

std::optional<Token> tokenFromJson(std::string_view data) {
    return readTokenDef(json::parse(data, nullptr, false));
}

std::optional<Mode> modeFromJson(std::string_view data) {
    return readMode(json::parse(data, nullptr, false));
}

} // namespace vc
