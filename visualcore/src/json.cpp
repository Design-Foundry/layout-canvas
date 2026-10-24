#include "vc/json.hpp"

#include <algorithm>
#include <array>
#include <unordered_map>
#include <unordered_set>
#include <nlohmann/json.hpp>

namespace vc {
namespace {

using nlohmann::json;

constexpr int kSnapshotVersion = 15;

constexpr std::array<std::pair<NodeType, std::string_view>, 9> kNodeTypes{{
    {NodeType::Frame, "frame"},
    {NodeType::Rect, "rect"},
    {NodeType::Ellipse, "ellipse"},
    {NodeType::Component, "component"},
    {NodeType::Instance, "instance"},
    {NodeType::Text, "text"},
    {NodeType::Line, "line"},
    {NodeType::Path, "path"},
    {NodeType::Image, "image"},
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

json writeValues(const ComponentValues& values) {
    json out = json::object();
    for (const auto& [name, value] : values) std::visit([&](const auto& v) { out[name] = v; }, value);
    return out;
}

void writePatch(json& out, const NodePatch& p) {
    if(p.image) out["image"]={{"width",p.image->width},{"height",p.image->height},{"pixels",p.image->pixels},{"id",p.image->id}};
    if(p.imageFit) out["imageFit"]=*p.imageFit;
    if(p.imageCrop) out["imageCrop"]={{"x",p.imageCrop->x},{"y",p.imageCrop->y},{"w",p.imageCrop->w},{"h",p.imageCrop->h}};
    if (p.path) {
        json points=json::array(); for(const auto point:p.path->points) points.push_back({{"x",point.x},{"y",point.y}});
        out["path"]={{"points",points},{"closed",p.path->closed}};
        if(!p.path->inHandles.empty()) {
            json incoming=json::array(),outgoing=json::array();
            for(const auto v:p.path->inHandles) incoming.push_back({{"x",v.x},{"y",v.y}});
            for(const auto v:p.path->outHandles) outgoing.push_back({{"x",v.x},{"y",v.y}});
            out["path"]["inHandles"]=incoming;out["path"]["outHandles"]=outgoing;
        }
        if(p.path->filled) out["path"]["filled"]=true;
    }
    if (p.lineWidth) out["lineWidth"] = *p.lineWidth;
    if (p.lineReverse) out["lineReverse"] = *p.lineReverse;
    if (p.typography) out["typography"] = p.typography->toString();
    if (p.textWrap) out["textWrap"] = *p.textWrap;
    if (p.textStyle) {
        const auto& t = *p.textStyle;
        out["textStyle"] = json::object();
        if (t.font) out["textStyle"]["font"] = *t.font;
        if (t.fontSize) out["textStyle"]["fontSize"] = *t.fontSize;
        if (t.lineHeight) out["textStyle"]["lineHeight"] = *t.lineHeight;
        if (t.letterSpacing) out["textStyle"]["letterSpacing"] = *t.letterSpacing;
        if (t.align) out["textStyle"]["align"] = *t.align == TextAlign::Center ? "center" : *t.align == TextAlign::Right ? "right" : "left";
    }
    if (p.text) {
        const auto& t = *p.text;
        out["text"] = {{"content", t.content}, {"font", t.font}, {"fontSize", t.fontSize},
            {"lineHeight", t.lineHeight}, {"letterSpacing", t.letterSpacing},
            {"align", t.align == TextAlign::Center ? "center" : t.align == TextAlign::Right ? "right" : "left"}};
    }
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
    if (p.type) out["type"] = typeName(*p.type);
    if (p.component && !p.component->isNil()) out["component"] = p.component->toString();
    if (p.componentValues) out["componentValues"] = writeValues(*p.componentValues);
}

json writeDefinition(const ComponentDefinition& definition) {
    json out{{"properties", json::array()}, {"axes", json::array()}, {"rules", json::array()}};
    if (!definition.texts.empty()) {
        out["texts"] = json::array();
        for (const auto& t : definition.texts) out["texts"].push_back({{"name", t.name}, {"target", t.target.toString()}, {"default", t.defaultValue}});
    }
    for (const auto& p : definition.properties) out["properties"].push_back({{"name", p.name}, {"target", p.target.toString()}, {"default", p.defaultValue}});
    for (const auto& a : definition.axes) out["axes"].push_back({{"name", a.name}, {"values", a.values}, {"default", a.defaultValue}});
    for (const auto& r : definition.rules) {
        json patch = json::object(); writePatch(patch, r.patch);
        out["rules"].push_back({{"when", r.when}, {"target", r.target.toString()}, {"patch", patch}});
    }
    if (!definition.swaps.empty()) {
        out["swaps"] = json::array();
        for (const auto& s : definition.swaps) {
            json choices = json::array(); for (const Id& id : s.choices) choices.push_back(id.toString());
            out["swaps"].push_back({{"name", s.name}, {"target", s.target.toString()}, {"default", s.defaultComponent.toString()}, {"choices", choices}});
        }
    }
    return out;
}

json writeOverrides(const std::vector<Override>& overrides) {
    json out = json::array();
    for (const Override& o : overrides) {
        json patch = json::object();
        writePatch(patch, o.patch);
        out.push_back({{"node", o.node.toString()}, {"patch", patch}});
    }
    return out;
}

/// The whole node, leaving out properties that still have their default value.
json write(const Node& n) {
    json out{{"id", n.id.toString()}, {"type", typeName(n.type)}, {"order", n.order}};
    if (!n.parent.isNil()) out["parent"] = n.parent.toString();
    NodePatch p = fullPatch(n);
    const Node defaults;
    if (!n.textWrap) p.textWrap.reset();
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
    if (n.component.isNil()) p.component.reset();
    if (n.componentValues.empty()) p.componentValues.reset();
    writePatch(out, p);
    if (!n.overrides.empty()) out["overrides"] = writeOverrides(n.overrides);
    if (!n.definition.empty()) out["definition"] = writeDefinition(n.definition);
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

std::optional<ComponentValues> readValues(const json& j) {
    if (!j.is_object() || j.size() > 200) return std::nullopt;
    ComponentValues values;
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (it.key().size() > 64) return std::nullopt;
        if (it->is_boolean()) values[it.key()] = it->get<bool>();
        else if (it->is_string() && it->get_ref<const std::string&>().size() <= 4096) values[it.key()] = it->get<std::string>();
        else return std::nullopt;
    }
    return values;
}

std::optional<NodePatch> readPatch(const json& j) {
    NodePatch p;
    if(j.contains("image")) {
        const auto& v=j["image"];
        if(!v.is_object()||v.size()!=4||!v.contains("width")||!v.contains("height")||!v.contains("pixels")||!v.contains("id")||!v["width"].is_number_integer()||!v["height"].is_number_integer()||!v["pixels"].is_string()||!v["id"].is_string()) return std::nullopt;
        const auto w=readNumber(v["width"]),h=readNumber(v["height"]);if(!w||!h||*w<1||*w>512||*h<1||*h>512) return std::nullopt;
        Raster image{int(*w),int(*h),v["pixels"].get<std::string>(),v["id"].get<std::string>()};
        if(!validRaster(image)) return std::nullopt;p.image=std::move(image);
    }
    if(!readField(j,"imageFit",p.imageFit,readString)) return std::nullopt;
    if(p.imageFit && *p.imageFit!="cover" && *p.imageFit!="contain" && *p.imageFit!="stretch") return std::nullopt;
    if(j.contains("imageCrop")) {
        const auto& c=j["imageCrop"];if(!c.is_object()||c.size()!=4||!c.contains("x")||!c.contains("y")||!c.contains("w")||!c.contains("h")) return std::nullopt;
        const auto x=readNumber(c["x"]),y=readNumber(c["y"]),w=readNumber(c["w"]),h=readNumber(c["h"]);
        if(!x||!y||!w||!h||*x<0||*y<0||*w<=0||*h<=0||*x+*w>1||*y+*h>1) return std::nullopt;
        p.imageCrop=ImageCrop{*x,*y,*w,*h};
    }
    if(j.contains("path")) {
        const auto& v=j["path"];
        if(!v.is_object() || !v.contains("closed") || !v["closed"].is_boolean() || !v.contains("points")) return std::nullopt;
        for(const auto& [key,value]:v.items()) if(key!="points" && key!="closed" && key!="inHandles" && key!="outHandles" && key!="filled") return std::nullopt;
        Path path;path.closed=v["closed"].get<bool>();
        if(v.contains("filled")) {if(!v["filled"].is_boolean()) return std::nullopt;path.filled=v["filled"].get<bool>();}
        const auto readPoints=[&](const json& values,std::vector<Vec2>& target) {
            if(!values.is_array() || values.size()>64) return false;
            for(const auto& point:values) {
                if(!point.is_object() || point.size()!=2 || !point.contains("x") || !point.contains("y")) return false;
                const auto x=readNumber(point["x"]),y=readNumber(point["y"]);if(!x||!y) return false;
                target.push_back({*x,*y});
            }
            return true;
        };
        if(!readPoints(v["points"],path.points)) return std::nullopt;
        if(v.contains("inHandles")!=v.contains("outHandles")) return std::nullopt;
        if(v.contains("inHandles") && (!readPoints(v["inHandles"],path.inHandles) || !readPoints(v["outHandles"],path.outHandles))) return std::nullopt;
        if(!validPath(path)) return std::nullopt;
        p.path=std::move(path);
    }
    if (!readField(j,"lineWidth",p.lineWidth,readNumber) || !readField(j,"lineReverse",p.lineReverse,readBool)) return std::nullopt;
    if (!readField(j, "typography", p.typography, readId)) return std::nullopt;
    if (!readField(j, "textWrap", p.textWrap, readBool)) return std::nullopt;
    if (j.contains("textStyle")) {
        const auto& v = j["textStyle"];
        if (!v.is_object() || v.empty() || v.size() > 5) return std::nullopt;
        TextStyle style; TextRun test;
        for (const auto& [key, value] : v.items()) {
            if (key == "font") {
                if (!value.is_string()) return std::nullopt;
                style.font = test.font = value.get<std::string>();
            } else if (key == "align") {
                if (!value.is_string()) return std::nullopt;
                const std::string align = value;
                if (align != "left" && align != "center" && align != "right") return std::nullopt;
                style.align = test.align = align == "center" ? TextAlign::Center : align == "right" ? TextAlign::Right : TextAlign::Left;
            } else {
                if (!value.is_number()) return std::nullopt;
                if (key == "fontSize") style.fontSize = test.fontSize = value;
                else if (key == "lineHeight") style.lineHeight = test.lineHeight = value;
                else if (key == "letterSpacing") style.letterSpacing = test.letterSpacing = value;
                else return std::nullopt;
            }
        }
        if (!validText(test)) return std::nullopt;
        p.textStyle = style;
    }
    if (j.contains("text")) {
        const auto& value = j["text"];
        if (!value.is_object() || value.size() != 6 || !value.contains("content") || !value["content"].is_string() ||
            !value.contains("font") || !value["font"].is_string() || !value.contains("fontSize") || !value["fontSize"].is_number() ||
            !value.contains("lineHeight") || !value["lineHeight"].is_number() || !value.contains("letterSpacing") || !value["letterSpacing"].is_number() ||
            !value.contains("align") || !value["align"].is_string()) return std::nullopt;
        const auto align = value["align"].get<std::string>();
        if (align != "left" && align != "center" && align != "right") return std::nullopt;
        TextRun text{value["content"], value["font"], value["fontSize"], value["lineHeight"], value["letterSpacing"],
            align == "center" ? TextAlign::Center : align == "right" ? TextAlign::Right : TextAlign::Left};
        if (!validText(text)) return std::nullopt;
        p.text = std::move(text);
    }
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
                    readField(j, "absolute", p.absolute, readBool) &&
                    readField(j, "type", p.type, [](const json& v) {
                        return v.is_string() ? typeFromName(v.get_ref<const std::string&>()) : std::nullopt;
                    }) &&
                    readField(j, "component", p.component, readId) &&
                    readField(j, "componentValues", p.componentValues, readValues);
    return ok ? std::optional{p} : std::nullopt;
}

std::optional<ComponentDefinition> readDefinition(const json& j) {
    if (!j.is_object() || j.size() != 3 + j.contains("swaps") + j.contains("texts") || !j.contains("properties") || !j["properties"].is_array() || j["properties"].size() > 64 ||
        !j.contains("axes") || !j["axes"].is_array() || j["axes"].size() > 8 ||
        !j.contains("rules") || !j["rules"].is_array() || j["rules"].size() > 256) return std::nullopt;
    ComponentDefinition d;
    if (j.contains("texts")) {
        if (!j["texts"].is_array() || j["texts"].size() > 64) return std::nullopt;
        for (const auto& t : j["texts"]) {
            if (!t.is_object() || t.size() != 3 || !t.contains("name") || !t["name"].is_string() || !t.contains("target") || !t.contains("default") || !t["default"].is_string()) return std::nullopt;
            const auto target = readId(t["target"]); if (!target) return std::nullopt;
            TextRun run; run.content = t["default"].get<std::string>(); if (!validText(run)) return std::nullopt;
            d.texts.push_back({t["name"].get<std::string>(), *target, run.content});
        }
    }
    if (j.contains("swaps")) {
        if (!j["swaps"].is_array() || j["swaps"].size() > 64) return std::nullopt;
        for (const auto& s : j["swaps"]) {
            if (!s.is_object() || s.size() != 4 || !s.contains("name") || !s["name"].is_string() ||
                !s.contains("target") || !s.contains("default") || !s.contains("choices") || !s["choices"].is_array() || s["choices"].size() > 32) return std::nullopt;
            const auto target = readId(s["target"]), selected = readId(s["default"]);
            if (!target || !selected) return std::nullopt;
            std::vector<Id> choices;
            for (const auto& item : s["choices"]) { const auto choice = readId(item); if (!choice) return std::nullopt; choices.push_back(*choice); }
            d.swaps.push_back({s["name"].get<std::string>(), *target, *selected, choices});
        }
    }
    for (const auto& p : j["properties"]) {
        if (!p.is_object() || p.size() != 3 || !p.contains("name") || !p["name"].is_string() || !p.contains("target") ||
            !p.contains("default") || !p["default"].is_boolean()) return std::nullopt;
        const auto target = readId(p["target"]); if (!target) return std::nullopt;
        d.properties.push_back({p["name"].get<std::string>(), *target, p["default"].get<bool>()});
    }
    for (const auto& a : j["axes"]) {
        if (!a.is_object() || a.size() != 3 || !a.contains("name") || !a["name"].is_string() || !a.contains("values") ||
            !a["values"].is_array() || a["values"].size() > 32 || !a.contains("default") || !a["default"].is_string()) return std::nullopt;
        std::vector<std::string> values;
        for (const auto& v : a["values"]) { if (!v.is_string()) return std::nullopt; values.push_back(v.get<std::string>()); }
        d.axes.push_back({a["name"].get<std::string>(), values, a["default"].get<std::string>()});
    }
    for (const auto& r : j["rules"]) {
        if (!r.is_object() || r.size() != 3 || !r.contains("when") || !r["when"].is_object() || r["when"].size() > 8 ||
            !r.contains("target") || !r.contains("patch") || !r["patch"].is_object()) return std::nullopt;
        const auto target = readId(r["target"]); const auto patch = readPatch(r["patch"]);
        if (!target || !patch) return std::nullopt;
        json canonical = json::object(); writePatch(canonical, *patch);
        if (canonical.size() != r["patch"].size()) return std::nullopt;
        std::map<std::string, std::string> when;
        for (auto it = r["when"].begin(); it != r["when"].end(); ++it) { if (!it->is_string()) return std::nullopt; when[it.key()] = it->get<std::string>(); }
        d.rules.push_back({when, *target, *patch});
    }
    return d;
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
    if (*type != NodeType::Line && patch->lineReverse) return std::nullopt;
    if (*type != NodeType::Line && *type != NodeType::Path && patch->lineWidth) return std::nullopt;
    if ((*type == NodeType::Path) != patch->path.has_value()) return std::nullopt;
    if((*type==NodeType::Image)!=patch->image.has_value()) return std::nullopt;
    if(*type!=NodeType::Image && (patch->imageFit || patch->imageCrop)) return std::nullopt;
    if (patch->textStyle) return std::nullopt;
    if ((*type == NodeType::Text) != patch->text.has_value()) return std::nullopt;
    applyPatch(n, *patch);
    if (j.contains("definition")) {
        auto definition = readDefinition(j["definition"]); if (!definition) return std::nullopt;
        n.definition = *definition;
    }
    n.type = *type;
    if (const auto it = j.find("overrides"); it != j.end()) {
        if (!it->is_array()) return std::nullopt;
        for (const json& item : *it) {
            const auto node = item.is_object() && item.contains("node") ? readId(item["node"]) : std::nullopt;
            const auto overridePatch = item.is_object() && item.contains("patch") ? readPatch(item["patch"]) : std::nullopt;
            if (!node || !overridePatch) return std::nullopt;
            n.overrides.push_back({*node, *overridePatch});
        }
    }
    return n;
}

std::optional<Id> readIdField(const json& j, const char* key) {
    return j.contains(key) ? readId(j[key]) : std::nullopt;
}

json writeModifier(const Modifier& modifier);

json writeLiteral(const TokenLiteral& literal) {
    if (const auto* style = std::get_if<TextStyle>(&literal)) {
        NodePatch patch; patch.textStyle=*style; json out=json::object(); writePatch(out,patch); return out["textStyle"];
    }
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
    if (type == TokenType::Typography) {
        if (j.size()!=5) return std::nullopt;
        const auto patch=readPatch(json{{"textStyle",j}});
        if (!patch || !patch->textStyle || !patch->textStyle->complete() || !patch->textStyle->font) return std::nullopt;
        return *patch->textStyle;
    }
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
    if (kind == "SetOverride") {
        const auto node = readIdField(j, "node");
        auto patch = j.contains("patch") && j["patch"].is_object() ? readPatch(j["patch"]) : std::nullopt;
        if (!node || !patch) return std::nullopt;
        return SetOverride{*id, *node, std::move(*patch)};
    }
    if (kind == "SetComponentDefinition") {
        const auto definition = j.contains("definition") ? readDefinition(j["definition"]) : std::nullopt;
        if (!definition) return std::nullopt;
        return SetComponentDefinition{*id, *definition};
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
            } else if constexpr (std::is_same_v<T, SetOverride>) {
                json patch = json::object();
                writePatch(patch, body.patch);
                return {{"id", body.instance.toString()}, {"node", body.node.toString()}, {"patch", patch}};
            } else if constexpr (std::is_same_v<T, SetComponentDefinition>) {
                return {{"id", body.component.toString()}, {"definition", writeDefinition(body.definition)}};
            } else {
                return {{"id", body.id.toString()}};
            }
        },
        op);
}

void writeLayers(const Document& document, const Id& parent, int depth, json& rows) {
    const auto& children = document.viewChildren(parent);
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        const Node& node = *document.view(*it);
        json row{{"id", node.id.toString()}, {"name", node.name}, {"type", typeName(node.type)},
                 {"depth", depth}, {"visible", node.visible}, {"locked", node.locked}};
        if (document.ownerOf(*it) != *it) row["inInstance"] = true;
        rows.push_back(std::move(row));
        writeLayers(document, node.id, depth + 1, rows);
    }
}

} // namespace

std::optional<OpBody> opFromJson(std::string_view kind, std::string_view data) {
    return readOp(kind, json::parse(data, nullptr, false));
}

std::string_view opKind(const OpBody& op) {
    constexpr std::array<std::string_view, 12> kKinds{"CreateNode",  "DeleteNode",  "SetProps",
                                                      "MoveNode",    "CreateToken", "DeleteToken",
                                                      "ReplaceToken", "CreateMode", "DeleteMode",
                                                      "ReplaceMode", "SetOverride", "SetComponentDefinition"};
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

std::string inspectNodeJson(const Document& document, const Node& source, const Id& mode) {
    // Resolve stored nodes through their component defaults; preserve caller-supplied previews.
    const Node* viewed = source.type == NodeType::Instance || document.find(source.id) == &source ? document.view(source.id) : nullptr;
    const Node& node = viewed ? *viewed : source;
    json out{{"id", node.id.toString()}, {"type", typeName(node.type)}, {"order", node.order}};
    if (node.type == NodeType::Text) out["textBound"] = document.isTextTarget(document.sourceOf(source.id));
    if (source.type == NodeType::Instance) {
        out["overrides"] = source.overrides.size();
        if (const Node* component = document.find(node.component)) out["componentName"] = component->name;
        out["swapBound"] = document.isSwapTarget(document.sourceOf(source.id));
    }
    if (source.type == NodeType::Component) out["inUse"] = document.isComponentUsed(source.id);
    if (const Id owner = document.ownerOf(source.id); owner != source.id) {
        out["inInstance"] = true;
        if (document.isSwapTarget(owner)) out["swapBound"] = true;
        const Id key = document.overrideKeyOf(source.id);
        const Node* instance = document.find(owner);
        out["overrides"] = instance && std::any_of(instance->overrides.begin(), instance->overrides.end(),
                                                  [&](const Override& o) { return o.node == key; }) ? 1 : 0;
    }
    if (!node.parent.isNil()) out["parent"] = node.parent.toString();
    writePatch(out, fullPatch(node));
    out["componentValues"] = writeValues(source.componentValues);
    if (source.type == NodeType::Component) out["definition"] = writeDefinition(source.definition);
    else if (source.type == NodeType::Instance) {
        if (const Node* main = document.find(node.component)) out["definition"] = writeDefinition(main->definition);
    }
    if (const auto* color = std::get_if<Oklch>(&node.fill)) {
        out["fill"]["hex"] = toHex(*color);
    }
    const Rect box = document.box(node.id, mode);
    out["box"] = {{"x", box.x}, {"y", box.y}, {"w", box.w}, {"h", box.h}};
    const Node* parent = document.view(node.parent);
    if (!parent) parent = document.find(node.parent);
    out["inFlow"] = parent && parent->layout.type != LayoutType::None && !node.absolute;
    out["parentLayout"] = parent ? nameOf(kLayoutTypes, parent->layout.type) : "none";
    return out.dump();
}

std::optional<NodePatch> patchFromJson(std::string_view data) {
    const json j = json::parse(data, nullptr, false);
    return j.is_object() ? readPatch(j) : std::nullopt;
}

std::optional<ComponentDefinition> componentDefinitionFromJson(std::string_view data) {
    const json j = json::parse(data, nullptr, false);
    return readDefinition(j);
}

std::string colorToJson(const Oklch& color) {
    return write(Value<Oklch>{color}).dump();
}

std::string frameLabelsToJson(const Document& document, const Camera& camera, const Id& mode) {
    json labels = json::array();
    for (const Id& id : document.children(Id{})) {
        const Node& node = *document.find(id);
        if (!isContainer(node.type) || !node.visible) continue;
        const Rect box = document.box(id, mode);
        const Vec2 topLeft = camera.toScreen({box.x, box.y});
        labels.push_back({{"id", id.toString()}, {"name", node.name}, {"type", typeName(node.type)},
                          {"x", topLeft.x}, {"y", topLeft.y},
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
    bool declared = false;
    bool swaps = false;
    bool text = false;
    bool textProperties = false;
    bool partialTextStyles = false;
    bool wrapping = false;
    bool fontSelection = false;
    bool oblique = false;
    bool typography = false;
    bool line = false;
    bool path = false;
    bool curved = false;
    bool image = false;
    for (const Token* token : document.tokens()) typography = typography || token->type == TokenType::Typography;
    const auto slanted = [](const std::string& font) { return font == "inter-oblique-v1" || font == "inter-bold-oblique-v1"; };
    document.forEachInPaintOrder([&](const Node& node) {
        nodes.push_back(write(node)); declared = declared || !node.definition.empty() || !node.componentValues.empty();
        typography = typography || !node.typography.isNil();
        line = line || node.type == NodeType::Line;
        path = path || node.type == NodeType::Path;
        curved = curved || node.path.filled || !node.path.inHandles.empty();
        image = image || node.type==NodeType::Image;
        swaps = swaps || !node.definition.swaps.empty();
        text = text || node.type == NodeType::Text;
        wrapping = wrapping || node.textWrap;
        fontSelection = fontSelection || node.text.font != "inter-regular-v1";
        oblique = oblique || slanted(node.text.font);
        textProperties = textProperties || !node.definition.texts.empty();
        const auto styleVersion = [&](const NodePatch& patch) {
            path = path || patch.path.has_value();
            image = image || patch.image || patch.imageFit || patch.imageCrop;
            curved = curved || (patch.path && (patch.path->filled || !patch.path->inHandles.empty()));
            oblique = oblique || (patch.text && slanted(patch.text->font)) || (patch.textStyle && patch.textStyle->font && slanted(*patch.textStyle->font));
            wrapping = wrapping || patch.textWrap.has_value();
            fontSelection = fontSelection || (patch.text && patch.text->font != "inter-regular-v1") ||
                (patch.textStyle && patch.textStyle->font.has_value());
            text = text || patch.text.has_value(); textProperties = textProperties || patch.textStyle.has_value();
            partialTextStyles = partialTextStyles || (patch.textStyle && !patch.textStyle->complete());
        };
        for (const auto& o : node.overrides) styleVersion(o.patch);
        for (const auto& r : node.definition.rules) styleVersion(r.patch);
    });
    return json{{"version", image ? 15 : curved ? 14 : path ? 13 : line ? 12 : typography ? 11 : oblique ? 10 : fontSelection ? 9 : wrapping ? 8 : partialTextStyles ? 7 : textProperties ? 6 : text ? 5 : swaps ? 4 : declared ? 3 : 2}, {"modes", modes}, {"tokens", tokens}, {"nodes", nodes}}.dump();
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
            if (version < 11 && token->type == TokenType::Typography) return std::nullopt;
            ops.push_back(CreateToken{std::move(*token)});
        }
    }
    std::vector<Node> nodes;
    std::vector<SetComponentDefinition> definitions;
    std::vector<SetProps> values;
    std::vector<SetOverride> overrides;
    std::unordered_map<Id, std::size_t> indexOf;
    for (const json& item : j["nodes"]) {
        auto node = readNode(item);
        if (!node) return std::nullopt;
        const auto imagePatch=[](const NodePatch& p){return p.image || p.imageFit || p.imageCrop;};
        if(version<15 && (node->type==NodeType::Image || std::any_of(node->overrides.begin(),node->overrides.end(),[&](const auto& o){return imagePatch(o.patch);}) || std::any_of(node->definition.rules.begin(),node->definition.rules.end(),[&](const auto& r){return imagePatch(r.patch);}))) return std::nullopt;
        const auto curvedPath=[](const Path& path){return path.filled || !path.inHandles.empty();};
        if(version<14 && (curvedPath(node->path) || std::any_of(node->overrides.begin(),node->overrides.end(),[&](const auto& o){return o.patch.path && curvedPath(*o.patch.path);}) || std::any_of(node->definition.rules.begin(),node->definition.rules.end(),[&](const auto& r){return r.patch.path && curvedPath(*r.patch.path);}))) return std::nullopt;
        if (version < 13 && (node->type == NodeType::Path || std::any_of(node->overrides.begin(),node->overrides.end(),[](const auto& o){return o.patch.path.has_value();}) || std::any_of(node->definition.rules.begin(),node->definition.rules.end(),[](const auto& r){return r.patch.path.has_value();}))) return std::nullopt;
        if (version < 12 && node->type == NodeType::Line) return std::nullopt;
        if (version < 11 && !node->typography.isNil()) return std::nullopt;
        if (version < 10) {
            const auto slanted = [](const std::string& font) { return font == "inter-oblique-v1" || font == "inter-bold-oblique-v1"; };
            const auto selected = [&](const NodePatch& patch) {
                return (patch.text && slanted(patch.text->font)) || (patch.textStyle && patch.textStyle->font && slanted(*patch.textStyle->font));
            };
            if (slanted(node->text.font)) return std::nullopt;
            for (const auto& o : node->overrides) if (selected(o.patch)) return std::nullopt;
            for (const auto& r : node->definition.rules) if (selected(r.patch)) return std::nullopt;
        }
        if (version < 9) {
            const auto selectedFont = [](const NodePatch& patch) {
                return (patch.text && patch.text->font != "inter-regular-v1") || (patch.textStyle && patch.textStyle->font.has_value());
            };
            if (node->text.font != "inter-regular-v1") return std::nullopt;
            for (const auto& o : node->overrides) if (selectedFont(o.patch)) return std::nullopt;
            for (const auto& r : node->definition.rules) if (selectedFont(r.patch)) return std::nullopt;
        }
        if (version < 8) {
            if (node->textWrap) return std::nullopt;
            for (const auto& o : node->overrides) if (o.patch.textWrap.has_value()) return std::nullopt;
            for (const auto& r : node->definition.rules) if (r.patch.textWrap.has_value()) return std::nullopt;
        }
        if (version < 6 && !node->definition.texts.empty()) return std::nullopt;
        if (version < 7) {
            for (const auto& o : node->overrides) if (o.patch.textStyle && !o.patch.textStyle->complete()) return std::nullopt;
            for (const auto& r : node->definition.rules) if (r.patch.textStyle && !r.patch.textStyle->complete()) return std::nullopt;
        }
        if (version < 6) {
            for (const auto& o : node->overrides) if (o.patch.textStyle) return std::nullopt;
            for (const auto& r : node->definition.rules) if (r.patch.textStyle) return std::nullopt;
        }
        if (version < 5) {
            if (node->type == NodeType::Text) return std::nullopt;
            for (const auto& o : node->overrides) if (o.patch.text) return std::nullopt;
            for (const auto& r : node->definition.rules) if (r.patch.text) return std::nullopt;
        }
        if (version >= 4) {
            for (const auto& o : node->overrides) overrides.push_back({node->id, o.node, o.patch});
            node->overrides.clear();
        }
        if (!node->definition.empty() || !node->componentValues.empty()) {
            if (version < 3) return std::nullopt;
            if (version < 4 && !node->definition.swaps.empty()) return std::nullopt;
            if (!node->definition.empty()) definitions.push_back({node->id, node->definition});
            if (!node->componentValues.empty()) { NodePatch p; p.componentValues = node->componentValues; values.push_back({node->id, p}); }
            node->definition = {}; node->componentValues = {};
        }
        indexOf.emplace(node->id, nodes.size());
        nodes.push_back(std::move(*node));
    }
    // Parents and the components instances copy must exist before the nodes that refer to them.
    std::vector<char> emitted(nodes.size(), 0);
    const auto emit = [&](std::size_t i, auto&& self) -> void {
        if (emitted[i]) return;
        emitted[i] = 1;
        for (const Id& dependency : {nodes[i].parent, nodes[i].component}) {
            if (const auto it = indexOf.find(dependency); it != indexOf.end()) self(it->second, self);
        }
        ops.push_back(CreateNode{std::move(nodes[i])});
    };
    for (std::size_t i = 0; i < nodes.size(); ++i) emit(i, emit);
    for (const auto& definition : definitions) ops.push_back(definition);
    for (const auto& value : values) ops.push_back(value);
    for (const auto& o : overrides) ops.push_back(o);
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
