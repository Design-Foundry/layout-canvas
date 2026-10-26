#include "vc/json.hpp"
#include "vc/diagnostics.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <nlohmann/json.hpp>

namespace vc {
namespace {

using nlohmann::json;

constexpr int kSnapshotVersion = 22;

bool statePrototype(const Interaction& i) { return i.action==PrototypeAction::ChangeState || i.transition==PrototypeTransition::SmartAnimate; }

bool extendedPrototype(const Interaction& interaction) {
    return interaction.action==PrototypeAction::OpenOverlay || interaction.action==PrototypeAction::CloseOverlay || interaction.transition!=PrototypeTransition::Instant;
}
bool positionedPrototype(const Interaction& interaction) {
    return (interaction.transition!=PrototypeTransition::Instant && interaction.transition!=PrototypeTransition::Dissolve) || interaction.placement!=OverlayPlacement::Center;
}

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

constexpr std::array<std::pair<PrototypeTrigger,std::string_view>,4> kPrototypeTriggers{{
    {PrototypeTrigger::Click,"click"},{PrototypeTrigger::Hover,"hover"},{PrototypeTrigger::Press,"press"},{PrototypeTrigger::Delay,"delay"}
}};
constexpr std::array<std::pair<ColorChannel, std::string_view>, 4> kChannels{{
    {ColorChannel::L, "l"}, {ColorChannel::C, "c"}, {ColorChannel::H, "h"}, {ColorChannel::Alpha, "alpha"}}};
constexpr std::array<std::pair<PrototypeOverflow, std::string_view>, 4> kPrototypeOverflows{{
    {PrototypeOverflow::None,"none"}, {PrototypeOverflow::Vertical,"vertical"},
    {PrototypeOverflow::Horizontal,"horizontal"}, {PrototypeOverflow::Both,"both"}}};
constexpr std::array<std::pair<PrototypeTransition, std::string_view>, 11> kPrototypeTransitions{{
    {PrototypeTransition::Instant,"instant"}, {PrototypeTransition::Dissolve,"dissolve"},
    {PrototypeTransition::SlideLeft,"slide-left"}, {PrototypeTransition::SlideRight,"slide-right"},
    {PrototypeTransition::SlideUp,"slide-up"}, {PrototypeTransition::SlideDown,"slide-down"},
    {PrototypeTransition::PushLeft,"push-left"}, {PrototypeTransition::PushRight,"push-right"},
    {PrototypeTransition::PushUp,"push-up"}, {PrototypeTransition::PushDown,"push-down"}, {PrototypeTransition::SmartAnimate,"smart-animate"}}};
constexpr std::array<std::pair<OverlayPlacement, std::string_view>, 10> kOverlayPlacements{{
    {OverlayPlacement::Center,"center"}, {OverlayPlacement::TopLeft,"top-left"},
    {OverlayPlacement::TopCenter,"top-center"}, {OverlayPlacement::TopRight,"top-right"},
    {OverlayPlacement::CenterLeft,"center-left"}, {OverlayPlacement::CenterRight,"center-right"},
    {OverlayPlacement::BottomLeft,"bottom-left"}, {OverlayPlacement::BottomCenter,"bottom-center"},
    {OverlayPlacement::BottomRight,"bottom-right"}, {OverlayPlacement::Manual,"manual"}}};
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
constexpr std::array<std::string_view, 12> kOpKinds{"CreateNode", "DeleteNode", "SetProps", "MoveNode", "CreateToken", "DeleteToken",
                                                      "ReplaceToken", "CreateMode", "DeleteMode", "ReplaceMode", "SetOverride", "SetComponentDefinition"};

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
    if(p.prototypeStart) out["prototypeStart"]=*p.prototypeStart;
    if(p.prototypeFlowName) out["prototypeFlowName"]=*p.prototypeFlowName;
    if(p.prototypeOverflow) out["prototypeOverflow"]=nameOf(kPrototypeOverflows,*p.prototypeOverflow);
    if(p.interaction) {
        switch(p.interaction->action) {
            case PrototypeAction::None: out["interaction"]=nullptr;break;
            case PrototypeAction::Navigate: out["interaction"]={{"trigger","click"},{"action","navigate"},{"target",p.interaction->target.toString()}};break;
            case PrototypeAction::Back: out["interaction"]={{"trigger","click"},{"action","back"}};break;
            case PrototypeAction::OpenOverlay: out["interaction"]={{"trigger","click"},{"action","open-overlay"},{"target",p.interaction->target.toString()}};break;
            case PrototypeAction::CloseOverlay: out["interaction"]={{"trigger","click"},{"action","close-overlay"}};break;
            case PrototypeAction::ChangeState: out["interaction"]={{"trigger","click"},{"action","change-state"},{"values",p.interaction->stateValues}};break;
        }
        if(p.interaction->transition!=PrototypeTransition::Instant) {
            out["interaction"]["transition"]=nameOf(kPrototypeTransitions,p.interaction->transition);
            out["interaction"]["durationMs"]=p.interaction->durationMs;
        }
        if(p.interaction->action==PrototypeAction::OpenOverlay && !p.interaction->dismissOutside) out["interaction"]["dismissOutside"]=false;
        if(p.interaction->action==PrototypeAction::OpenOverlay && p.interaction->placement!=OverlayPlacement::Center) out["interaction"]["placement"]=nameOf(kOverlayPlacements,p.interaction->placement);
        if(p.interaction->action==PrototypeAction::OpenOverlay && p.interaction->placement==OverlayPlacement::Manual) out["interaction"]["offset"]={{"x",p.interaction->overlayOffset.x},{"y",p.interaction->overlayOffset.y}};
    }
    if(p.interactions) {
        out["interactions"]=json::array();
        for(const auto& entry:*p.interactions) {
            json action;NodePatch patch;patch.interaction=entry.interaction;writePatch(action,patch);
            auto encoded=action["interaction"];
            encoded["trigger"]=nameOf(kPrototypeTriggers,entry.trigger);
            if(entry.trigger==PrototypeTrigger::Delay) encoded["delayMs"]=entry.delayMs;
            out["interactions"].push_back(std::move(encoded));
        }
    }
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

/// Frames and components start without a fill; every other type starts with a visible one.
bool defaultFillVisible(NodeType type) { return type != NodeType::Frame && type != NodeType::Component; }

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

// Readers return nullopt or false when a present field has the wrong shape. Absent fields are fine.

enum class Shape { String, Boolean, Number };

constexpr std::string_view kNodeTypeNames = "frame, rect, ellipse, component, instance, text, line, path or image";

std::string slot(std::size_t index) {
    return "[" + std::to_string(index) + "]";
}

bool require(const json& j, const char* key, Shape shape) {
    DiagnosticScope scope{key};
    const auto it = j.find(key);
    if (it == j.end()) return refuse(std::string{key} + " is required");
    if (shape == Shape::String ? it->is_string() : shape == Shape::Boolean ? it->is_boolean() : it->is_number()) return true;
    return refuse(std::string{key} + (shape == Shape::String ? " must be a string" : shape == Shape::Boolean ? " must be a boolean" : " must be a number"));
}

std::optional<Id> parseId(const json& j) {
    return j.is_string() ? Id::parse(j.get_ref<const std::string&>()) : std::nullopt;
}

std::optional<Id> readId(const json& j) {
    auto id = parseId(j);
    if (!id) return refused("must be a UUID string");
    return id;
}

std::optional<Id> requireId(const json& j, const char* key) {
    DiagnosticScope scope{key};
    const auto it = j.find(key);
    if (it == j.end()) return refused(std::string{key} + " is required: a UUID string");
    return readId(*it);
}

std::optional<std::string> requireOrder(const json& j) {
    DiagnosticScope scope{"order"};
    const auto it = j.find("order");
    if (it == j.end() || !it->is_string()) return refused("order is required: a fractional index string such as a0");
    return it->get<std::string>();
}

std::optional<TokenRef> readToken(const json& j) {
    if (!j.is_object() || j.size() != 1) return std::nullopt;
    const auto it = j.find("token");
    if (it == j.end()) return std::nullopt;
    const auto id = parseId(*it);
    return id ? std::optional{TokenRef{*id}} : std::nullopt;
}

bool isTokenRefShape(const json& j) {
    return j.is_object() && j.size() == 1 && j.contains("token");
}

std::optional<Value<Oklch>> readColor(const json& j) {
    if (auto ref = readToken(j)) return *ref;
    if (isTokenRefShape(j)) return refused("token must be a UUID string");
    if (!j.is_object()) return refused("must be an object {l, c, h, a} of numbers or a {token} reference");
    Oklch c;
    const std::array<std::pair<const char*, double*>, 4> fields{{{"l", &c.l}, {"c", &c.c}, {"h", &c.h}, {"a", &c.a}}};
    for (const auto& [key, field] : fields) {
        if (!require(j, key, Shape::Number)) return std::nullopt;
        *field = j.find(key)->get<double>();
    }
    return c;
}

std::optional<Value<double>> readLength(const json& j) {
    if (j.is_number()) return j.get<double>();
    if (auto ref = readToken(j)) return *ref;
    if (isTokenRefShape(j)) return refused("token must be a UUID string");
    return refused("must be a number or a {token} reference");
}

template <typename T, typename Reader>
bool readField(const json& j, const char* key, std::optional<T>& out, Reader reader) {
    const auto it = j.find(key);
    if (it == j.end()) return true;
    DiagnosticScope scope{key};
    auto value = reader(*it);
    if (!value) return refuse("has the wrong shape");
    out = std::move(*value);
    return true;
}

template <typename E, std::size_t N>
bool readNamed(const json& j, const char* key, const std::array<std::pair<E, std::string_view>, N>& names, E& out, std::string_view message) {
    const auto it = j.find(key);
    if (it == j.end()) return true;
    DiagnosticScope scope{key};
    const auto value = valueOf(names, *it);
    if (!value) return refuse(message);
    out = *value;
    return true;
}

std::optional<double> readNumber(const json& j) {
    if (!j.is_number()) return refused("must be a number");
    return j.get<double>();
}

std::optional<bool> readBool(const json& j) {
    if (!j.is_boolean()) return refused("must be a boolean");
    return j.get<bool>();
}

std::optional<std::string> readString(const json& j) {
    if (!j.is_string()) return refused("must be a string");
    return j.get<std::string>();
}

std::optional<NodeType> readNodeType(const json& j) {
    const auto type = j.is_string() ? typeFromName(j.get_ref<const std::string&>()) : std::nullopt;
    if (!type) return refused("type must be " + std::string{kNodeTypeNames});
    return type;
}

std::optional<std::array<std::optional<Value<double>>, 4>> readFour(const json& j) {
    if (!j.is_array() || j.size() != 4) return refused("must be an array of 4 lengths: numbers, {token} references or null");
    std::array<std::optional<Value<double>>, 4> out{};
    for (std::size_t i = 0; i < 4; ++i) {
        if (j[i].is_null()) continue;
        DiagnosticScope item{slot(i)};
        out[i] = readLength(j[i]);
        if (!out[i]) return std::nullopt;
    }
    return out;
}

std::optional<Value<Stroke>> readStroke(const json& j) {
    if (auto ref = readToken(j)) return *ref;
    if (isTokenRefShape(j)) return refused("token must be a UUID string");
    if (!j.is_object()) return refused("must be an object {width, style, color, align, sides} or a {token} reference");
    Stroke stroke;
    std::optional<Value<double>> width;
    std::optional<Value<Oklch>> color;
    std::optional<std::array<std::optional<Value<double>>, 4>> sides;
    if (!readField(j, "width", width, readLength) || !readField(j, "color", color, readColor)) return std::nullopt;
    if (width) stroke.width = *width;
    if (color) stroke.color = *color;
    if (!readNamed(j, "style", kStrokeStyles, stroke.style, "style must be solid, none, dashed, dotted or double") ||
        !readNamed(j, "align", kStrokeAligns, stroke.align, "align must be inside, center or outside") ||
        !readField(j, "sides", sides, readFour)) {
        return std::nullopt;
    }
    if (sides) stroke.sides = *sides;
    return stroke;
}

std::optional<Value<std::vector<Shadow>>> readShadows(const json& j) {
    if (auto ref = readToken(j)) return *ref;
    if (isTokenRefShape(j)) return refused("token must be a UUID string");
    if (!j.is_array()) return refused("must be an array of shadow layers or a {token} reference");
    std::vector<Shadow> layers;
    for (std::size_t index = 0; index < j.size(); ++index) {
        const json& item = j[index];
        DiagnosticScope entry{slot(index)};
        if (!item.is_object()) return refused("must be an object with x, y, blur, spread, color, inset and visible");
        Shadow layer;
        const std::array<std::pair<const char*, double*>, 4> numbers{
            {{"x", &layer.x}, {"y", &layer.y}, {"blur", &layer.blur}, {"spread", &layer.spread}}};
        for (const auto& [key, field] : numbers) {
            if (const auto it = item.find(key); it != item.end()) {
                DiagnosticScope number{key};
                if (!it->is_number()) return refused("must be a number");
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
    if (!j.is_object()) return refused("must be an object with type, gap, padding, justify and align");
    Layout layout;
    if (!readNamed(j, "type", kLayoutTypes, layout.type, "type must be none, row or column") ||
        !readNamed(j, "justify", kJustifies, layout.justify, "justify must be start, center, end, spaceBetween, spaceAround or spaceEvenly") ||
        !readNamed(j, "align", kAligns, layout.align, "align must be start, center, end or stretch")) {
        return std::nullopt;
    }
    std::optional<Value<double>> gap;
    if (!readField(j, "gap", gap, readLength)) return std::nullopt;
    if (gap) layout.gap = *gap;
    if (const auto it = j.find("padding"); it != j.end()) {
        DiagnosticScope field{"padding"};
        const auto padding = readFour(*it);
        if (!padding) return std::nullopt;
        for (std::size_t i = 0; i < 4; ++i) layout.padding[i] = (*padding)[i].value_or(0.0);
    }
    return layout;
}

std::optional<SizeLimits> readLimits(const json& j) {
    if (!j.is_object()) return refused("must be an object with minWidth, maxWidth, minHeight and maxHeight");
    SizeLimits limits;
    const bool ok = readField(j, "minWidth", limits.minWidth, readLength) &&
                    readField(j, "maxWidth", limits.maxWidth, readLength) &&
                    readField(j, "minHeight", limits.minHeight, readLength) &&
                    readField(j, "maxHeight", limits.maxHeight, readLength);
    return ok ? std::optional{limits} : std::nullopt;
}

std::optional<Sizing> readSizing(const json& j) {
    const auto sizing = valueOf(kSizings, j);
    if (!sizing) return refused("sizing must be fixed, hug or fill");
    return sizing;
}

std::optional<ComponentValues> readValues(const json& j) {
    if (!j.is_object() || j.size() > 200) return refused("must be an object of at most 200 values");
    ComponentValues values;
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (it.key().size() > 64) return refused("value names are at most 64 characters");
        DiagnosticScope entry{it.key()};
        if (it->is_boolean()) values[it.key()] = it->get<bool>();
        else if (it->is_string() && it->get_ref<const std::string&>().size() <= 4096) values[it.key()] = it->get<std::string>();
        else return refused("must be a boolean or a string of at most 4096 characters");
    }
    return values;
}

std::optional<TextStyle> readTextStyle(const json& v) {
    if (!v.is_object() || v.empty() || v.size() > 5) return refused("must be an object with 1 to 5 of font, fontSize, lineHeight, letterSpacing and align");
    TextStyle style; TextRun test;
    for (const auto& [key, value] : v.items()) {
        DiagnosticScope field{key};
        if (key == "font") {
            if (!value.is_string()) return refused("font must be a string");
            style.font = test.font = value.get<std::string>();
        } else if (key == "align") {
            if (!value.is_string()) return refused("align must be left, center or right");
            const std::string align = value;
            if (align != "left" && align != "center" && align != "right") return refused("align must be left, center or right");
            style.align = test.align = align == "center" ? TextAlign::Center : align == "right" ? TextAlign::Right : TextAlign::Left;
        } else {
            if (key != "fontSize" && key != "lineHeight" && key != "letterSpacing") return refused("unknown key, use font, fontSize, lineHeight, letterSpacing or align");
            if (!value.is_number()) return refused(key + " must be a number");
            if (key == "fontSize") style.fontSize = test.fontSize = value;
            else if (key == "lineHeight") style.lineHeight = test.lineHeight = value;
            else style.letterSpacing = test.letterSpacing = value;
        }
    }
    if (const char* why = textProblem(test)) return refused(why);
    return style;
}

std::optional<NodePatch> readPatch(const json& j) {
    NodePatch p;
    if(!readField(j,"prototypeStart",p.prototypeStart,readBool)) return std::nullopt;
    if(!readField(j,"prototypeFlowName",p.prototypeFlowName,readString)) return std::nullopt;
    if(p.prototypeFlowName && !validPrototypeFlowName(*p.prototypeFlowName)) {DiagnosticScope field{"prototypeFlowName"};return refused("flow name must be single-line text of at most 128 UTF-8 bytes");}
    if(j.contains("prototypeOverflow")) {
        DiagnosticScope field{"prototypeOverflow"};
        const auto value=valueOf(kPrototypeOverflows,j["prototypeOverflow"]);
        if(!value) return refused("prototypeOverflow must be none, vertical, horizontal or both");
        p.prototypeOverflow=*value;
    }
    if(j.contains("interaction")) {
        DiagnosticScope interactionField{"interaction"};
        const auto& i=j["interaction"];
        if(i.is_null()) p.interaction=Interaction{};
        else {
            if(!i.is_object()) return refused("interaction must be an object or null");
            if(!i.contains("trigger") || i["trigger"]!="click") { DiagnosticScope field{"trigger"};return refused("only click triggers are supported"); }
            if(!i.contains("action") || !i["action"].is_string()) { DiagnosticScope field{"action"};return refused("an action is required"); }
            if((i["action"]=="back" || i["action"]=="close-overlay") && i.size()==2) p.interaction=Interaction{i["action"]=="back"?PrototypeAction::Back:PrototypeAction::CloseOverlay,{}};
            else if(((i["action"]=="navigate" || i["action"]=="open-overlay") && i.contains("target")) || i["action"]=="change-state") {
                Interaction interaction;
                if(i["action"]=="change-state") {
                    interaction.action=PrototypeAction::ChangeState;
                    if(i.contains("target")) {DiagnosticScope field{"target"};return refused("change-state resolves its own instance and takes no target");}
                    if(!i.contains("values")) {DiagnosticScope field{"values"};return refused("change-state needs variant axis values");}
                } else {
                    const auto id=[&] {DiagnosticScope field{"target"};return readId(i["target"]);}();
                    if(!id||id->isNil()) {DiagnosticScope field{"target"};return refused("target must be a non-nil frame UUID");}
                    interaction.action=i["action"]=="navigate"?PrototypeAction::Navigate:PrototypeAction::OpenOverlay;interaction.target=*id;
                }
                for(auto field=i.begin();field!=i.end();++field) {
                    if(field.key()=="trigger" || field.key()=="action" || field.key()=="target") continue;
                    DiagnosticScope option{field.key()};
                    if(field.key()=="values" && interaction.action==PrototypeAction::ChangeState) {
                        if(!field->is_object() || field->empty() || field->size()>8) return refused("values must hold 1 to 8 variant axis selections");
                        for(auto value=field->begin();value!=field->end();++value) {
                            DiagnosticScope axis{value.key()};
                            if(!value->is_string()) return refused("variant selections must be strings");
                            interaction.stateValues.emplace(value.key(),value->get<std::string>());
                        }
                    } else if(field.key()=="transition") {
                        const auto transition=valueOf(kPrototypeTransitions,*field);
                        if(!transition) return refused("transition must be instant, dissolve, smart-animate, slide-left/right/up/down or push-left/right/up/down");
                        interaction.transition=*transition;
                    } else if(field.key()=="durationMs") {
                        if(!field->is_number_integer()) return refused("durationMs must be an integer from 1 to 2000");
                        const auto number=readNumber(*field);
                        if(!number || *number<1 || *number>2000) return refused("durationMs must be an integer from 1 to 2000");
                        interaction.durationMs=int(*number);
                    } else if(field.key()=="dismissOutside" && interaction.action==PrototypeAction::OpenOverlay) {
                        if(!field->is_boolean()) return refused("dismissOutside must be a boolean");
                        interaction.dismissOutside=field->get<bool>();
                    } else if(field.key()=="placement" && interaction.action==PrototypeAction::OpenOverlay) {
                        const auto placement=valueOf(kOverlayPlacements,*field);
                        if(!placement) return refused("placement must be center, top-left/center/right, center-left/right, bottom-left/center/right or manual");
                        interaction.placement=*placement;
                    } else if(field.key()=="offset" && interaction.action==PrototypeAction::OpenOverlay) {
                        if(!field->is_object() || field->size()!=2 || !field->contains("x") || !field->contains("y")) return refused("offset must contain exactly numeric x and y");
                        for(const auto* key:{"x","y"}) {
                            DiagnosticScope axis{key};
                            const auto number=readNumber((*field)[key]);
                            if(!number || !std::isfinite(*number) || *number < -100000 || *number > 100000) return refused("overlay offset must be finite and between -100000 and 100000");
                            if(std::string_view(key)=="x") interaction.overlayOffset.x=*number;
                            else interaction.overlayOffset.y=*number;
                        }
                    } else return refused("unsupported option for this interaction action");
                }
                if(i.contains("durationMs") && interaction.transition==PrototypeTransition::Instant) {DiagnosticScope field{"durationMs"};return refused("durationMs requires a non-instant transition");}
                if(interaction.action==PrototypeAction::OpenOverlay) {
                    if(interaction.placement==OverlayPlacement::Manual && !i.contains("offset")) {DiagnosticScope field{"offset"};return refused("manual overlay placement requires an offset");}
                    if(interaction.placement!=OverlayPlacement::Manual && i.contains("offset")) {DiagnosticScope field{"offset"};return refused("offset requires manual overlay placement");}
                }
                if(interaction.action==PrototypeAction::OpenOverlay && interaction.transition>=PrototypeTransition::PushLeft && interaction.transition<=PrototypeTransition::PushDown) {DiagnosticScope field{"transition"};return refused("push transitions are supported only for navigate actions");}
                if(!validInteractionSettings(interaction)) return refused("invalid prototype action settings");
                p.interaction=interaction;
            } else return refused("use navigate/open-overlay with a target, change-state with values, or back/close-overlay without options");
        }
    }
    if(j.contains("interactions")) {
        DiagnosticScope scope{"interactions"};const auto& list=j["interactions"];
        if(!list.is_array() || list.size()>16) return refused("interactions must be an ordered array with at most 16 entries");
        p.interactions=std::vector<TriggeredInteraction>{};
        for(std::size_t index=0;index<list.size();++index) {
            DiagnosticScope entryScope{slot(index)};auto entry=list[index];
            if(!entry.is_object() || !entry.contains("trigger")) return refused("an interaction entry needs a trigger and action");
            const auto trigger=valueOf(kPrototypeTriggers,entry["trigger"]);
            if(!trigger) {DiagnosticScope field{"trigger"};return refused("trigger must be click, hover, press or delay");}
            int delay=0;
            if(*trigger==PrototypeTrigger::Delay) {
                DiagnosticScope field{"delayMs"};
                if(!entry.contains("delayMs") || !entry["delayMs"].is_number_integer()) return refused("delay trigger needs integer delayMs from 1 to 60000");
                const auto number=readNumber(entry["delayMs"]);
                if(!number || *number<1 || *number>60000) return refused("delayMs must be from 1 to 60000");
                delay=int(*number);entry.erase("delayMs");
            } else if(entry.contains("delayMs")) {DiagnosticScope field{"delayMs"};return refused("delayMs is only supported with a delay trigger");}
            entry["trigger"]="click";
            const auto parsed=readPatch(json{{"interaction",entry}});
            if(!parsed || !parsed->interaction || parsed->interaction->action==PrototypeAction::None) return refused("an interaction entry needs a supported action");
            p.interactions->push_back({*trigger,*parsed->interaction,delay});
        }
    }
    if(p.interaction && p.interaction->action!=PrototypeAction::None && p.interactions && !p.interactions->empty()) return refused("interaction and interactions cannot both be active");
    if(j.contains("image")) {
        DiagnosticScope scope{"image"};
        const auto& v=j["image"];
        if(!v.is_object()||v.size()!=4||!v.contains("width")||!v.contains("height")||!v.contains("pixels")||!v.contains("id")) return refused("must be an object with exactly width, height, pixels and id");
        if(!v["width"].is_number_integer()||!v["height"].is_number_integer()) return refused("width and height must be integers");
        if(!v["pixels"].is_string()||!v["id"].is_string()) return refused("pixels and id must be strings");
        const auto w=readNumber(v["width"]),h=readNumber(v["height"]);if(!w||!h||*w<1||*w>512||*h<1||*h>512) return refused("width and height must be between 1 and 512");
        Raster image{int(*w),int(*h),v["pixels"].get<std::string>(),v["id"].get<std::string>()};
        if(!validRaster(image)) return refused("pixels do not match the width, height and id");p.image=std::move(image);
    }
    if(!readField(j,"imageFit",p.imageFit,readString)) return std::nullopt;
    if(p.imageFit && *p.imageFit!="cover" && *p.imageFit!="contain" && *p.imageFit!="stretch") {DiagnosticScope scope{"imageFit"};return refused("imageFit must be cover, contain or stretch");}
    if(j.contains("imageCrop")) {
        DiagnosticScope scope{"imageCrop"};
        const auto& c=j["imageCrop"];if(!c.is_object()||c.size()!=4||!c.contains("x")||!c.contains("y")||!c.contains("w")||!c.contains("h")) return refused("must be an object with exactly x, y, w and h");
        for(const char* key:{"x","y","w","h"}) if(!require(c,key,Shape::Number)) return std::nullopt;
        const auto x=readNumber(c["x"]),y=readNumber(c["y"]),w=readNumber(c["w"]),h=readNumber(c["h"]);
        if(*x<0||*y<0||*w<=0||*h<=0||*x+*w>1||*y+*h>1) return refused("must be a normalized rect inside 0 to 1 with positive w and h");
        p.imageCrop=ImageCrop{*x,*y,*w,*h};
    }
    if(j.contains("path")) {
        DiagnosticScope scope{"path"};
        const auto& v=j["path"];
        if(!v.is_object()) return refused("must be an object with closed, points and optionally filled, inHandles and outHandles");
        if(!require(v,"closed",Shape::Boolean)) return std::nullopt;
        if(!v.contains("points")) {DiagnosticScope field{"points"};return refused("points is required");}
        for(const auto& [key,value]:v.items()) if(key!="points" && key!="closed" && key!="inHandles" && key!="outHandles" && key!="filled") {DiagnosticScope field{key};return refused("unknown key, use points, closed, inHandles, outHandles or filled");}
        Path path;path.closed=v["closed"].get<bool>();
        if(v.contains("filled")) {if(!require(v,"filled",Shape::Boolean)) return std::nullopt;path.filled=v["filled"].get<bool>();}
        const auto readPoints=[&](const char* name,const json& values,std::vector<Vec2>& target) {
            DiagnosticScope list{name};
            if(!values.is_array() || values.size()>64) return refuse("must be an array of at most 64 points");
            for(std::size_t i=0;i<values.size();++i) {
                const auto& point=values[i];
                DiagnosticScope entry{slot(i)};
                if(!point.is_object() || point.size()!=2) return refuse("must be an object with exactly x and y numbers");
                if(!require(point,"x",Shape::Number) || !require(point,"y",Shape::Number)) return false;
                target.push_back({point["x"].get<double>(),point["y"].get<double>()});
            }
            return true;
        };
        if(!readPoints("points",v["points"],path.points)) return std::nullopt;
        if(v.contains("inHandles")!=v.contains("outHandles")) return refused("inHandles and outHandles must be given together");
        if(v.contains("inHandles") && (!readPoints("inHandles",v["inHandles"],path.inHandles) || !readPoints("outHandles",v["outHandles"],path.outHandles))) return std::nullopt;
        if(const char* why=pathProblem(path)) return refused(why);
        p.path=std::move(path);
    }
    if (!readField(j,"lineWidth",p.lineWidth,readNumber) || !readField(j,"lineReverse",p.lineReverse,readBool)) return std::nullopt;
    if (!readField(j, "typography", p.typography, readId)) return std::nullopt;
    if (!readField(j, "textWrap", p.textWrap, readBool)) return std::nullopt;
    if (j.contains("textStyle")) {
        DiagnosticScope scope{"textStyle"};
        const auto style = readTextStyle(j["textStyle"]);
        if (!style) return std::nullopt;
        p.textStyle = *style;
    }
    if (j.contains("text")) {
        DiagnosticScope scope{"text"};
        const auto& value = j["text"];
        if (!value.is_object()) return refused("must be an object with exactly content, font, fontSize, lineHeight, letterSpacing and align");
        const std::array<std::pair<const char*, Shape>, 6> keys{{{"content", Shape::String}, {"font", Shape::String}, {"fontSize", Shape::Number},
            {"lineHeight", Shape::Number}, {"letterSpacing", Shape::Number}, {"align", Shape::String}}};
        for (const auto& [key, shape] : keys) if (!require(value, key, shape)) return std::nullopt;
        if (value.size() != 6) return refused("has keys other than content, font, fontSize, lineHeight, letterSpacing and align");
        const auto align = value["align"].get<std::string>();
        if (align != "left" && align != "center" && align != "right") {DiagnosticScope field{"align"};return refused("align must be left, center or right");}
        TextRun text{value["content"], value["font"], value["fontSize"], value["lineHeight"], value["letterSpacing"],
            align == "center" ? TextAlign::Center : align == "right" ? TextAlign::Right : TextAlign::Left};
        if (const char* why = textProblem(text)) return refused(why);
        p.text = std::move(text);
    }
    const bool fillCleared = j.contains("fill") && j["fill"].is_null();
    const bool ok = readField(j, "name", p.name, readString) && readField(j, "x", p.x, readNumber) &&
                    readField(j, "y", p.y, readNumber) && readField(j, "w", p.w, readNumber) &&
                    readField(j, "h", p.h, readNumber) && (fillCleared || readField(j, "fill", p.fill, readColor)) &&
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
                    readField(j, "type", p.type, readNodeType) &&
                    readField(j, "component", p.component, readId) &&
                    readField(j, "componentValues", p.componentValues, readValues);
    if (ok && fillCleared && !p.fillVisible) p.fillVisible = false;
    return ok ? std::optional{p} : std::nullopt;
}

std::optional<ComponentDefinition> readDefinition(const json& j) {
    if (!j.is_object()) return refused("must be an object with properties, axes and rules, and optionally texts and swaps");
    const std::array<std::pair<const char*, std::size_t>, 3> required{{{"properties", 64}, {"axes", 8}, {"rules", 256}}};
    for (const auto& [key, limit] : required) {
        DiagnosticScope field{key};
        const auto it = j.find(key);
        if (it == j.end()) return refused(std::string{key} + " is required");
        if (!it->is_array() || it->size() > limit) return refused(std::string{key} + " must be an array of at most " + std::to_string(limit) + " entries");
    }
    if (j.size() != 3 + j.contains("swaps") + j.contains("texts")) return refused("has keys other than properties, axes, rules, texts and swaps");
    ComponentDefinition d;
    if (j.contains("texts")) {
        DiagnosticScope list{"texts"};
        if (!j["texts"].is_array() || j["texts"].size() > 64) return refused("texts must be an array of at most 64 entries");
        for (std::size_t i = 0; i < j["texts"].size(); ++i) {
            const auto& t = j["texts"][i];
            DiagnosticScope entry{slot(i)};
            if (!t.is_object() || t.size() != 3) return refused("must be an object with exactly name, target and default");
            if (!require(t, "name", Shape::String)) return std::nullopt;
            const auto target = requireId(t, "target"); if (!target) return std::nullopt;
            if (!require(t, "default", Shape::String)) return std::nullopt;
            TextRun run; run.content = t["default"].get<std::string>();
            if (const char* why = textProblem(run)) {DiagnosticScope field{"default"};return refused(why);}
            d.texts.push_back({t["name"].get<std::string>(), *target, run.content});
        }
    }
    if (j.contains("swaps")) {
        DiagnosticScope list{"swaps"};
        if (!j["swaps"].is_array() || j["swaps"].size() > 64) return refused("swaps must be an array of at most 64 entries");
        for (std::size_t i = 0; i < j["swaps"].size(); ++i) {
            const auto& s = j["swaps"][i];
            DiagnosticScope entry{slot(i)};
            if (!s.is_object() || s.size() != 4) return refused("must be an object with exactly name, target, default and choices");
            if (!require(s, "name", Shape::String)) return std::nullopt;
            const auto target = requireId(s, "target"), selected = requireId(s, "default");
            if (!target || !selected) return std::nullopt;
            const auto choicesIt = s.find("choices");
            DiagnosticScope field{"choices"};
            if (choicesIt == s.end() || !choicesIt->is_array() || choicesIt->size() > 32) return refused("choices is required: an array of at most 32 component ids");
            std::vector<Id> choices;
            for (std::size_t c = 0; c < choicesIt->size(); ++c) {
                DiagnosticScope choiceEntry{slot(c)};
                const auto choice = readId((*choicesIt)[c]); if (!choice) return std::nullopt;
                choices.push_back(*choice);
            }
            d.swaps.push_back({s["name"].get<std::string>(), *target, *selected, choices});
        }
    }
    {
        DiagnosticScope list{"properties"};
        for (std::size_t i = 0; i < j["properties"].size(); ++i) {
            const auto& p = j["properties"][i];
            DiagnosticScope entry{slot(i)};
            if (!p.is_object() || p.size() != 3) return refused("must be an object with exactly name, target and default");
            if (!require(p, "name", Shape::String)) return std::nullopt;
            const auto target = requireId(p, "target"); if (!target) return std::nullopt;
            if (!require(p, "default", Shape::Boolean)) return std::nullopt;
            d.properties.push_back({p["name"].get<std::string>(), *target, p["default"].get<bool>()});
        }
    }
    {
        DiagnosticScope list{"axes"};
        for (std::size_t i = 0; i < j["axes"].size(); ++i) {
            const auto& a = j["axes"][i];
            DiagnosticScope entry{slot(i)};
            if (!a.is_object() || a.size() != 3) return refused("must be an object with exactly name, values and default");
            if (!require(a, "name", Shape::String)) return std::nullopt;
            const auto valuesIt = a.find("values");
            {
                DiagnosticScope field{"values"};
                if (valuesIt == a.end() || !valuesIt->is_array() || valuesIt->size() > 32) return refused("values is required: an array of at most 32 strings");
            }
            if (!require(a, "default", Shape::String)) return std::nullopt;
            std::vector<std::string> values;
            DiagnosticScope field{"values"};
            for (std::size_t v = 0; v < valuesIt->size(); ++v) {
                DiagnosticScope valueEntry{slot(v)};
                if (!(*valuesIt)[v].is_string()) return refused("must be a string");
                values.push_back((*valuesIt)[v].get<std::string>());
            }
            d.axes.push_back({a["name"].get<std::string>(), values, a["default"].get<std::string>()});
        }
    }
    {
        DiagnosticScope list{"rules"};
        for (std::size_t i = 0; i < j["rules"].size(); ++i) {
            const auto& r = j["rules"][i];
            DiagnosticScope entry{slot(i)};
            if (!r.is_object() || r.size() != 3) return refused("must be an object with exactly when, target and patch");
            const auto whenIt = r.find("when");
            std::map<std::string, std::string> when;
            {
                DiagnosticScope field{"when"};
                if (whenIt == r.end() || !whenIt->is_object() || whenIt->size() > 8) return refused("when is required: an object of at most 8 axis values");
                for (auto it = whenIt->begin(); it != whenIt->end(); ++it) {
                    DiagnosticScope axis{it.key()};
                    if (!it->is_string()) return refused("must be a string");
                    when[it.key()] = it->get<std::string>();
                }
            }
            const auto target = requireId(r, "target"); if (!target) return std::nullopt;
            DiagnosticScope field{"patch"};
            const auto patchIt = r.find("patch");
            if (patchIt == r.end() || !patchIt->is_object()) return refused("patch is required: an object");
            const auto patch = readPatch(*patchIt);
            if (!patch) return std::nullopt;
            json canonical = json::object(); writePatch(canonical, *patch);
            if (canonical.size() != patchIt->size()) {
                for (auto it = patchIt->begin(); it != patchIt->end(); ++it) {
                    DiagnosticScope key{it.key()};
                    if (!canonical.contains(it.key())) return refused("unknown patch key, or a value that is not written back");
                }
                return refused("has keys that are unknown or not written back");
            }
            d.rules.push_back({when, *target, *patch});
        }
    }
    return d;
}

std::optional<Node> readNode(const json& j) {
    if (!j.is_object()) return refused("a node must be an object");
    Node n;

    const auto id = requireId(j, "id");
    if (!id) return std::nullopt;
    std::optional<NodeType> type;
    {
        DiagnosticScope field{"type"};
        const auto it = j.find("type");
        if (it == j.end()) return refused("type is required: " + std::string{kNodeTypeNames});
        type = readNodeType(*it);
        if (!type) return std::nullopt;
    }
    const auto order = requireOrder(j);
    if (!order) return std::nullopt;
    n.id = *id;
    n.type = *type;
    n.order = *order;
    n.fillVisible = defaultFillVisible(n.type) || (j.contains("fill") && !j["fill"].is_null());

    if (j.contains("parent")) {
        const auto parent = requireId(j, "parent");
        if (!parent) return std::nullopt;
        n.parent = *parent;
    }

    const auto patch = readPatch(j);
    if (!patch) return std::nullopt;
    const auto misplaced = [](const char* key, std::string_view message) {
        DiagnosticScope field{key};
        return refused(message);
    };
    if (*type != NodeType::Line && patch->lineReverse) return misplaced("lineReverse", "only line nodes take lineReverse");
    if (*type != NodeType::Line && *type != NodeType::Path && patch->lineWidth) return misplaced("lineWidth", "only line and path nodes take lineWidth");
    if (*type == NodeType::Path && !patch->path) return misplaced("path", "path nodes need a path");
    if (*type != NodeType::Path && patch->path) return misplaced("path", "only path nodes take a path");
    if (*type == NodeType::Image && !patch->image) return misplaced("image", "image nodes need an image");
    if (*type != NodeType::Image && patch->image) return misplaced("image", "only image nodes take an image");
    if (*type != NodeType::Image && patch->imageFit) return misplaced("imageFit", "only image nodes take imageFit");
    if (*type != NodeType::Image && patch->imageCrop) return misplaced("imageCrop", "only image nodes take imageCrop");
    if (*type == NodeType::Instance && (patch->interaction || patch->interactions)) return misplaced("interaction", "instance interactions use component-root overrides");
    if (patch->prototypeFlowName && (*type != NodeType::Frame || !n.parent.isNil())) return misplaced("prototypeFlowName", "only root frames take flow names");
    if (patch->prototypeStart && (*type != NodeType::Frame || !n.parent.isNil())) return misplaced("prototypeStart", "only root frames take flow starting flags");
    if (patch->prototypeOverflow && (*type != NodeType::Frame || !n.parent.isNil())) return misplaced("prototypeOverflow", "only root frames take prototype overflow");
    if (patch->textStyle) return misplaced("textStyle", "nodes store their full text, use text instead of textStyle");
    if (*type == NodeType::Text && !patch->text) return misplaced("text", "text nodes need text");
    if (*type != NodeType::Text && patch->text) return misplaced("text", "only text nodes take text");
    applyPatch(n, *patch);
    if (j.contains("definition")) {
        DiagnosticScope field{"definition"};
        auto definition = readDefinition(j["definition"]); if (!definition) return std::nullopt;
        n.definition = *definition;
    }
    n.type = *type;
    if (const auto it = j.find("overrides"); it != j.end()) {
        DiagnosticScope field{"overrides"};
        if (!it->is_array()) return refused("overrides must be an array of {node, patch} objects");
        for (std::size_t i = 0; i < it->size(); ++i) {
            const json& item = (*it)[i];
            DiagnosticScope entry{slot(i)};
            if (!item.is_object()) return refused("must be an object with node and patch");
            const auto node = requireId(item, "node");
            if (!node) return std::nullopt;
            DiagnosticScope patchField{"patch"};
            if (!item.contains("patch")) return refused("patch is required: an object");
            const auto overridePatch = readPatch(item["patch"]);
            if (!overridePatch) return std::nullopt;
            n.overrides.push_back({*node, *overridePatch});
        }
    }
    return n;
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
    if (!j.is_object()) return refused("a token value must be an object");
    if (type == TokenType::Typography) {
        if (j.size()!=5) return refused("a typography value needs exactly font, fontSize, lineHeight, letterSpacing and align");
        const auto style=readTextStyle(j);
        if (!style) return std::nullopt;
        if (!style->complete() || !style->font) return refused("a typography value needs exactly font, fontSize, lineHeight, letterSpacing and align");
        return *style;
    }
    if (type == TokenType::Border) {
        const auto width = requireId(j, "width");
        if (!width) return std::nullopt;
        const auto color = requireId(j, "color");
        if (!color) return std::nullopt;
        StrokeStyle style = StrokeStyle::Solid;
        if (!readNamed(j, "style", kStrokeStyles, style, "style must be solid, none, dashed, dotted or double")) return std::nullopt;
        return BorderValue{*width, style, *color};
    }
    if (type == TokenType::Shadow) {
        ShadowValue shadow;
        {
            DiagnosticScope field{"layers"};
            if (!j.contains("layers") || !j["layers"].is_array()) return refused("layers is required: an array of shadow layers");
        }
        DiagnosticScope list{"layers"};
        for (std::size_t index = 0; index < j["layers"].size(); ++index) {
            const json& item = j["layers"][index];
            DiagnosticScope entry{slot(index)};
            if (!item.is_object()) return refused("must be an object with offsetX, offsetY, blur, spread, color, inset and colorModifiers");
            ShadowLayerValue layer;
            const std::array<std::pair<const char*, double*>, 4> numbers{
                {{"offsetX", &layer.x}, {"offsetY", &layer.y}, {"blur", &layer.blur}, {"spread", &layer.spread}}};
            for (const auto& [key, field] : numbers) {
                if (const auto it = item.find(key); it != item.end()) {
                    if (!require(item, key, Shape::Number)) return std::nullopt;
                    *field = it->get<double>();
                }
            }
            const auto color = requireId(item, "color");
            if (!color) return std::nullopt;
            layer.color = *color;
            if (item.contains("inset")) {
                if (!require(item, "inset", Shape::Boolean)) return std::nullopt;
                layer.inset = item["inset"].get<bool>();
            }
            if (const auto it = item.find("colorModifiers"); it != item.end()) {
                DiagnosticScope modifiers{"colorModifiers"};
                if (!it->is_array()) return refused("colorModifiers must be an array of modifiers");
                for (std::size_t m = 0; m < it->size(); ++m) {
                    DiagnosticScope modifierEntry{slot(m)};
                    auto modifier = readModifier((*it)[m]);
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
            if (!require(j, key, Shape::Number)) return std::nullopt;
            *field = j.find(key)->get<double>();
        }
        return color;
    }
    if (!require(j, "value", Shape::Number)) return std::nullopt;
    Magnitude magnitude{j.find("value")->get<double>(), Unit::None};
    if (const auto unit = j.find("unit"); unit != j.end()) {
        DiagnosticScope field{"unit"};
        const auto parsed = unit->is_string() ? unitFromName(unit->get_ref<const std::string&>()) : std::nullopt;
        if (!parsed) return refused("unit must be px, rem, em, pt, ms, s or empty");
        magnitude.unit = *parsed;
    }
    return magnitude;
}

bool readOperand(const json& j, std::optional<double>& value, std::optional<Id>& ref) {
    if (const auto it = j.find("value"); it != j.end()) {
        if (!require(j, "value", Shape::Number)) return false;
        value = it->get<double>();
    }
    if (const auto it = j.find("ref"); it != j.end()) {
        DiagnosticScope field{"ref"};
        ref = readId(*it);
        if (!ref) return false;
    }
    return true;
}

std::optional<Modifier> readModifier(const json& j) {
    if (!j.is_object()) return refused("a modifier must be an object");
    if (j.contains("mix")) {
        const auto token = requireId(j, "mix");
        if (!token || !require(j, "amount", Shape::Number)) return std::nullopt;
        return ColorMixModifier{*token, j.find("amount")->get<double>()};
    }
    if (j.contains("channel")) {
        ColorChannelModifier channel;
        {
            DiagnosticScope field{"channel"};
            const auto name = valueOf(kChannels, j["channel"]);
            if (!name) return refused("channel must be l, c, h or alpha");
            channel.channel = *name;
        }
        {
            DiagnosticScope field{"kind"};
            const auto op = j.contains("kind") ? valueOf(kColorOps, j["kind"]) : std::nullopt;
            if (!op) return refused("kind must be set, delta or factor for a color channel modifier");
            channel.op = *op;
        }
        if (!readOperand(j, channel.value, channel.ref)) return std::nullopt;
        return channel;
    }
    MagnitudeModifier step;
    {
        DiagnosticScope field{"kind"};
        const auto op = j.contains("kind") ? valueOf(kMagnitudeOps, j["kind"]) : std::nullopt;
        if (!op) return refused("kind must be factor, delta, min or max, or give channel or mix for a color modifier");
        step.op = *op;
    }
    if (!readOperand(j, step.value, step.ref)) return std::nullopt;
    return step;
}

std::optional<Token> readTokenDef(const json& j) {
    if (!j.is_object()) return refused("a token must be an object");
    Token token;
    const auto id = requireId(j, "id");
    if (!id) return std::nullopt;
    std::optional<TokenType> type;
    {
        DiagnosticScope field{"type"};
        const auto it = j.find("type");
        const std::string_view allowed = "color, dimension, duration, ratio, opacity, border, shadow or typography";
        if (it == j.end()) return refused("type is required: " + std::string{allowed});
        type = it->is_string() ? tokenTypeFromName(it->get_ref<const std::string&>()) : std::nullopt;
        if (!type) return refused("type must be " + std::string{allowed});
    }
    token.id = *id;
    token.type = *type;

    const std::array<std::pair<const char*, std::string*>, 4> texts{{{"identifier", &token.identifier},
        {"order", &token.order}, {"displayName", &token.displayName}, {"description", &token.description}}};
    for (const auto& [key, field] : texts) {
        if (const auto it = j.find(key); it != j.end()) {
            if (!require(j, key, Shape::String)) return std::nullopt;
            *field = it->get<std::string>();
        }
    }
    if (const auto it = j.find("deprecated"); it != j.end()) {
        if (!require(j, "deprecated", Shape::Boolean)) return std::nullopt;
        token.deprecated = it->get<bool>();
    }
    if (const auto it = j.find("value"); it != j.end()) {
        DiagnosticScope field{"value"};
        token.value = readLiteral(token.type, *it);
        if (!token.value) return std::nullopt;
    }
    if (const auto it = j.find("source"); it != j.end()) {
        DiagnosticScope field{"source"};
        if (!it->is_object()) return refused("source must be an object mapping mode ids to token ids");
        for (const auto& [mode, target] : it->items()) {
            const auto modeId = Id::parse(mode);
            if (!modeId) return refused("source keys must be mode ids (UUID strings)");
            DiagnosticScope entry{mode};
            const auto targetId = readId(target);
            if (!targetId) return std::nullopt;
            token.source[*modeId] = *targetId;
        }
    }
    if (const auto it = j.find("modifiers"); it != j.end()) {
        DiagnosticScope field{"modifiers"};
        if (!it->is_array()) return refused("modifiers must be an array of modifier objects");
        for (std::size_t i = 0; i < it->size(); ++i) {
            DiagnosticScope entry{slot(i)};
            auto modifier = readModifier((*it)[i]);
            if (!modifier) return std::nullopt;
            token.modifiers.push_back(std::move(*modifier));
        }
    }
    return token;
}

std::optional<Mode> readMode(const json& j) {
    if (!j.is_object()) return refused("a mode must be an object with id, name and order");
    const auto id = requireId(j, "id");
    if (!id) return std::nullopt;
    if (!require(j, "name", Shape::String)) return std::nullopt;
    std::string order;
    if (j.contains("order")) {
        if (!require(j, "order", Shape::String)) return std::nullopt;
        order = j["order"].get<std::string>();
    }
    return Mode{*id, j["name"].get<std::string>(), order};
}

std::optional<NodePatch> requirePatch(const json& j) {
    DiagnosticScope field{"patch"};
    const auto it = j.find("patch");
    if (it == j.end() || !it->is_object()) return refused("patch is required: an object");
    return readPatch(*it);
}

std::string opKindList() {
    std::string list;
    for (const auto kind : kOpKinds) list += (list.empty() ? "" : ", ") + std::string{kind};
    return list;
}

std::optional<OpBody> readOp(std::string_view kind, const json& j) {
    if (std::find(kOpKinds.begin(), kOpKinds.end(), kind) == kOpKinds.end()) {
        return refused("unknown op kind " + std::string{kind} + ", expected one of " + opKindList());
    }
    if (!j.is_object()) return refused("op data must be an object");

    if (kind == "CreateToken" || kind == "ReplaceToken") {
        DiagnosticScope field{"token"};
        if (!j.contains("token")) return refused("token is required: a token definition");
        auto token = readTokenDef(j["token"]);
        if (!token) return std::nullopt;
        if (kind == "CreateToken") return CreateToken{std::move(*token)};
        return ReplaceToken{std::move(*token)};
    }
    if (kind == "CreateMode" || kind == "ReplaceMode") {
        DiagnosticScope field{"mode"};
        if (!j.contains("mode")) return refused("mode is required: a mode object");
        auto mode = readMode(j["mode"]);
        if (!mode) return std::nullopt;
        if (kind == "CreateMode") return CreateMode{std::move(*mode)};
        return ReplaceMode{std::move(*mode)};
    }

    if (kind == "CreateNode") {
        DiagnosticScope field{"node"};
        if (!j.contains("node")) return refused("node is required: a node object");
        auto node = readNode(j["node"]);
        if (!node) return std::nullopt;
        return CreateNode{std::move(*node)};
    }
    const auto id = requireId(j, "id");
    if (!id) return std::nullopt;

    if (kind == "DeleteNode") {
        return DeleteNode{*id};
    }
    if (kind == "DeleteToken") {
        return DeleteToken{*id};
    }
    if (kind == "SetOverride") {
        const auto node = requireId(j, "node");
        if (!node) return std::nullopt;
        auto patch = requirePatch(j);
        if (!patch) return std::nullopt;
        return SetOverride{*id, *node, std::move(*patch)};
    }
    if (kind == "SetComponentDefinition") {
        DiagnosticScope field{"definition"};
        if (!j.contains("definition")) return refused("definition is required: a component definition");
        const auto definition = readDefinition(j["definition"]);
        if (!definition) return std::nullopt;
        return SetComponentDefinition{*id, *definition};
    }
    if (kind == "DeleteMode") {
        return DeleteMode{*id};
    }
    if (kind == "SetProps") {
        auto patch = requirePatch(j);
        if (!patch) return std::nullopt;
        return SetProps{*id, std::move(*patch)};
    }
    if (kind == "MoveNode") {
        const auto order = requireOrder(j);
        if (!order) return std::nullopt;
        Id parent;
        if (j.contains("parent")) {
            const auto p = requireId(j, "parent");
            if (!p) return std::nullopt;
            parent = *p;
        }
        return MoveNode{*id, parent, *order};
    }
    return refused("unknown op kind " + std::string{kind} + ", expected one of " + opKindList());
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
    return kOpKinds[op.index()];
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

std::optional<std::vector<Op>> opsFromJson(std::string_view data) {
    const json j = json::parse(data, nullptr, false);
    if (!j.is_array()) return refused(j.is_discarded() ? "ops is not valid JSON" : "ops must be an array of {kind, data} objects");
    DiagnosticScope list{"ops"};
    std::vector<Op> ops;
    ops.reserve(j.size());
    for (std::size_t i = 0; i < j.size(); ++i) {
        const json& item = j[i];
        DiagnosticScope entry{slot(i)};
        if (!item.is_object() || !item.contains("kind") || !item["kind"].is_string() || !item.contains("data")) {
            return refused("must be an object with a kind string and data");
        }
        std::optional<Id> id = Id{};
        if (item.contains("id")) id = requireId(item, "id");
        auto op = readOp(item["kind"].get_ref<const std::string&>(), item["data"]);
        if (!id || !op) return std::nullopt;
        ops.push_back({*id, std::move(*op)});
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
    if (!j.is_object()) return refused("patch must be a JSON object");
    return readPatch(j);
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
    bool prototype = false;
    bool extended = false;
    bool positioned = false;
    bool scrolling = false;
    bool multipleInteractions = false;
    bool componentStates = false;
    bool namedFlows = false;
    for (const Token* token : document.tokens()) typography = typography || token->type == TokenType::Typography;
    const auto slanted = [](const std::string& font) { return font == "inter-oblique-v1" || font == "inter-bold-oblique-v1"; };
    document.forEachInPaintOrder([&](const Node& node) {
        nodes.push_back(write(node)); declared = declared || !node.definition.empty() || !node.componentValues.empty();
        typography = typography || !node.typography.isNil();
        line = line || node.type == NodeType::Line;
        path = path || node.type == NodeType::Path;
        curved = curved || node.path.filled || !node.path.inHandles.empty();
        image = image || node.type==NodeType::Image;
        prototype = prototype || node.interaction!=Interaction{} || node.prototypeStart;
        extended = extended || extendedPrototype(node.interaction);
        positioned = positioned || positionedPrototype(node.interaction);
        namedFlows = namedFlows || !node.prototypeFlowName.empty();
        scrolling = scrolling || node.prototypeOverflow!=PrototypeOverflow::None;
        multipleInteractions = multipleInteractions || !node.interactions.empty();
        componentStates = componentStates || statePrototype(node.interaction) || std::any_of(node.interactions.begin(),node.interactions.end(),[](const auto& e){return statePrototype(e.interaction);});
        swaps = swaps || !node.definition.swaps.empty();
        text = text || node.type == NodeType::Text;
        wrapping = wrapping || node.textWrap;
        fontSelection = fontSelection || node.text.font != "inter-regular-v1";
        oblique = oblique || slanted(node.text.font);
        textProperties = textProperties || !node.definition.texts.empty();
        const auto styleVersion = [&](const NodePatch& patch) {
            path = path || patch.path.has_value();
            image = image || patch.image || patch.imageFit || patch.imageCrop;
            prototype = prototype || patch.interaction.has_value() || patch.prototypeStart.has_value();
            extended = extended || (patch.interaction && extendedPrototype(*patch.interaction));
            positioned = positioned || (patch.interaction && positionedPrototype(*patch.interaction));
            namedFlows = namedFlows || patch.prototypeFlowName.has_value();
            scrolling = scrolling || patch.prototypeOverflow.has_value();
            multipleInteractions = multipleInteractions || patch.interactions.has_value();
            componentStates = componentStates || (patch.interaction && statePrototype(*patch.interaction)) || (patch.interactions && std::any_of(patch.interactions->begin(),patch.interactions->end(),[](const auto& e){return statePrototype(e.interaction);}));
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
    return json{{"version", namedFlows ? 22 : componentStates ? 21 : multipleInteractions ? 20 : scrolling ? 19 : positioned ? 18 : extended ? 17 : prototype ? 16 : image ? 15 : curved ? 14 : path ? 13 : line ? 12 : typography ? 11 : oblique ? 10 : fontSelection ? 9 : wrapping ? 8 : partialTextStyles ? 7 : textProperties ? 6 : text ? 5 : swaps ? 4 : declared ? 3 : 2}, {"modes", modes}, {"tokens", tokens}, {"nodes", nodes}}.dump();
}

std::optional<std::vector<OpBody>> snapshotFromJson(std::string_view data) {
    const json j = json::parse(data, nullptr, false);
    if (!j.is_object()) return refused(j.is_discarded() ? "snapshot is not valid JSON" : "snapshot must be a JSON object");
    {
        DiagnosticScope field{"version"};
        if (!j.contains("version") || !j["version"].is_number_integer()) return refused("version is required: an integer from 1 to " + std::to_string(kSnapshotVersion));
    }
    const int version = j["version"].get<int>();
    if (version < 1 || version > kSnapshotVersion) {
        DiagnosticScope field{"version"};
        return refused("version must be from 1 to " + std::to_string(kSnapshotVersion));
    }
    if (!j.contains("nodes") || !j["nodes"].is_array()) {
        DiagnosticScope field{"nodes"};
        return refused("nodes is required: an array of nodes");
    }

    std::vector<OpBody> ops;
    if (version >= 2) {
        for (const char* key : {"modes", "tokens"}) {
            if (!j.contains(key) || !j[key].is_array()) {
                DiagnosticScope field{key};
                return refused(std::string{key} + " is required from snapshot version 2: an array");
            }
        }
        {
            DiagnosticScope list{"modes"};
            for (std::size_t i = 0; i < j["modes"].size(); ++i) {
                DiagnosticScope entry{slot(i)};
                auto mode = readMode(j["modes"][i]);
                if (!mode) return std::nullopt;
                if (mode->id == kDefaultModeId) {
                    ops.push_back(ReplaceMode{std::move(*mode)});
                } else {
                    ops.push_back(CreateMode{std::move(*mode)});
                }
            }
        }
        DiagnosticScope list{"tokens"};
        for (std::size_t i = 0; i < j["tokens"].size(); ++i) {
            DiagnosticScope entry{slot(i)};
            auto token = readTokenDef(j["tokens"][i]);
            if (!token) return std::nullopt;
            if (version < 11 && token->type == TokenType::Typography) {
                DiagnosticScope field{"type"};
                return refused("typography tokens need snapshot version 11 or later");
            }
            ops.push_back(CreateToken{std::move(*token)});
        }
    }
    std::vector<Node> nodes;
    std::vector<SetComponentDefinition> definitions;
    std::vector<SetProps> values;
    std::vector<SetProps> prototypeProps;
    std::vector<SetOverride> overrides;
    std::unordered_map<Id, std::size_t> indexOf;
    DiagnosticScope nodeList{"nodes"};
    for (std::size_t index = 0; index < j["nodes"].size(); ++index) {
        DiagnosticScope entry{slot(index)};
        auto node = readNode(j["nodes"][index]);
        if (!node) return std::nullopt;
        if(version<22 && (j["nodes"][index].contains("prototypeFlowName") || std::any_of(node->overrides.begin(),node->overrides.end(),[](const auto& o){return o.patch.prototypeFlowName.has_value();}) || std::any_of(node->definition.rules.begin(),node->definition.rules.end(),[](const auto& r){return r.patch.prototypeFlowName.has_value();}))) return refused("named flows need snapshot version 22 or later");
        const auto statePatch=[](const NodePatch& p){return (p.interaction && statePrototype(*p.interaction)) || (p.interactions && std::any_of(p.interactions->begin(),p.interactions->end(),[](const auto& e){return statePrototype(e.interaction);}));};
        if(version<21 && (statePrototype(node->interaction) || std::any_of(node->interactions.begin(),node->interactions.end(),[](const auto& e){return statePrototype(e.interaction);}) || std::any_of(node->overrides.begin(),node->overrides.end(),[&](const auto& o){return statePatch(o.patch);}) || std::any_of(node->definition.rules.begin(),node->definition.rules.end(),[&](const auto& r){return statePatch(r.patch);}))) return refused("component state actions and smart-animate need snapshot version 21 or later");
        const auto multiplePatch=[](const NodePatch& p){return p.interactions.has_value();};
        if(version<20 && (j["nodes"][index].contains("interactions") || !node->interactions.empty() ||
            std::any_of(node->overrides.begin(),node->overrides.end(),[&](const auto& o){return multiplePatch(o.patch);}) ||
            std::any_of(node->definition.rules.begin(),node->definition.rules.end(),[&](const auto& r){return multiplePatch(r.patch);}))) return refused("ordered interactions need snapshot version 20 or later");
        if(version<19 && (node->prototypeOverflow!=PrototypeOverflow::None || j["nodes"][index].contains("prototypeOverflow"))) return refused("prototype overflow needs snapshot version 19 or later");
        const auto prototypePatch=[](const NodePatch& p){return p.interaction.has_value() || p.prototypeStart.has_value();};
        const auto extendedPatch=[](const NodePatch& p){return p.interaction && extendedPrototype(*p.interaction);};
        const auto positionedPatch=[](const NodePatch& p){return p.interaction && positionedPrototype(*p.interaction);};
        if(version<18 && (positionedPrototype(node->interaction) || std::any_of(node->overrides.begin(),node->overrides.end(),[&](const auto& o){return positionedPatch(o.patch);}) || std::any_of(node->definition.rules.begin(),node->definition.rules.end(),[&](const auto& rule){return positionedPatch(rule.patch);}))) return refused("slide, push and positioned overlay interactions need snapshot version 18 or later");
        if(version<17 && (extendedPrototype(node->interaction) || std::any_of(node->overrides.begin(),node->overrides.end(),[&](const auto& o){return extendedPatch(o.patch);}) || std::any_of(node->definition.rules.begin(),node->definition.rules.end(),[&](const auto& rule){return extendedPatch(rule.patch);}))) return refused("overlay and dissolve interactions need snapshot version 17 or later");
        if(version<16 && (node->interaction!=Interaction{} || node->prototypeStart || std::any_of(node->overrides.begin(),node->overrides.end(),[&](const auto& o){return prototypePatch(o.patch);}) || std::any_of(node->definition.rules.begin(),node->definition.rules.end(),[&](const auto& rule){return prototypePatch(rule.patch);}))) return refused("prototype metadata needs snapshot version 16 or later");
        if(node->interaction!=Interaction{} || !node->interactions.empty() || node->prototypeStart) {
            NodePatch p;
            if(node->interaction!=Interaction{}) p.interaction=node->interaction;
            if(!node->interactions.empty()) p.interactions=node->interactions;
            if(node->prototypeStart) p.prototypeStart=true;
            prototypeProps.push_back({node->id,p});node->interaction={};node->interactions.clear();node->prototypeStart=false;
        }
        const auto imagePatch=[](const NodePatch& p){return p.image || p.imageFit || p.imageCrop;};
        if(version<15 && (node->type==NodeType::Image || std::any_of(node->overrides.begin(),node->overrides.end(),[&](const auto& o){return imagePatch(o.patch);}) || std::any_of(node->definition.rules.begin(),node->definition.rules.end(),[&](const auto& r){return imagePatch(r.patch);}))) return refused("image layers need snapshot version 15 or later");
        const auto curvedPath=[](const Path& path){return path.filled || !path.inHandles.empty();};
        if(version<14 && (curvedPath(node->path) || std::any_of(node->overrides.begin(),node->overrides.end(),[&](const auto& o){return o.patch.path && curvedPath(*o.patch.path);}) || std::any_of(node->definition.rules.begin(),node->definition.rules.end(),[&](const auto& r){return r.patch.path && curvedPath(*r.patch.path);}))) return refused("curved or filled paths need snapshot version 14 or later");
        if (version < 13 && (node->type == NodeType::Path || std::any_of(node->overrides.begin(),node->overrides.end(),[](const auto& o){return o.patch.path.has_value();}) || std::any_of(node->definition.rules.begin(),node->definition.rules.end(),[](const auto& r){return r.patch.path.has_value();}))) return refused("path layers need snapshot version 13 or later");
        if (version < 12 && node->type == NodeType::Line) return refused("line layers need snapshot version 12 or later");
        if (version < 11 && !node->typography.isNil()) return refused("text bound to shared typography needs snapshot version 11 or later");
        if (version < 10) {
            const auto slanted = [](const std::string& font) { return font == "inter-oblique-v1" || font == "inter-bold-oblique-v1"; };
            const auto selected = [&](const NodePatch& patch) {
                return (patch.text && slanted(patch.text->font)) || (patch.textStyle && patch.textStyle->font && slanted(*patch.textStyle->font));
            };
            constexpr std::string_view oblique = "oblique fonts need snapshot version 10 or later";
            if (slanted(node->text.font)) return refused(oblique);
            for (const auto& o : node->overrides) if (selected(o.patch)) return refused(oblique);
            for (const auto& r : node->definition.rules) if (selected(r.patch)) return refused(oblique);
        }
        if (version < 9) {
            const auto selectedFont = [](const NodePatch& patch) {
                return (patch.text && patch.text->font != "inter-regular-v1") || (patch.textStyle && patch.textStyle->font.has_value());
            };
            constexpr std::string_view fonts = "selecting a font needs snapshot version 9 or later";
            if (node->text.font != "inter-regular-v1") return refused(fonts);
            for (const auto& o : node->overrides) if (selectedFont(o.patch)) return refused(fonts);
            for (const auto& r : node->definition.rules) if (selectedFont(r.patch)) return refused(fonts);
        }
        if (version < 8) {
            constexpr std::string_view wrapping = "text wrapping needs snapshot version 8 or later";
            if (node->textWrap) return refused(wrapping);
            for (const auto& o : node->overrides) if (o.patch.textWrap.has_value()) return refused(wrapping);
            for (const auto& r : node->definition.rules) if (r.patch.textWrap.has_value()) return refused(wrapping);
        }
        if (version < 6 && !node->definition.texts.empty()) return refused("component text properties need snapshot version 6 or later");
        if (version < 7) {
            constexpr std::string_view partial = "partial text styles need snapshot version 7 or later";
            for (const auto& o : node->overrides) if (o.patch.textStyle && !o.patch.textStyle->complete()) return refused(partial);
            for (const auto& r : node->definition.rules) if (r.patch.textStyle && !r.patch.textStyle->complete()) return refused(partial);
        }
        if (version < 6) {
            constexpr std::string_view styles = "text styles in overrides and rules need snapshot version 6 or later";
            for (const auto& o : node->overrides) if (o.patch.textStyle) return refused(styles);
            for (const auto& r : node->definition.rules) if (r.patch.textStyle) return refused(styles);
        }
        if (version < 5) {
            constexpr std::string_view text = "text layers need snapshot version 5 or later";
            if (node->type == NodeType::Text) return refused(text);
            for (const auto& o : node->overrides) if (o.patch.text) return refused(text);
            for (const auto& r : node->definition.rules) if (r.patch.text) return refused(text);
        }
        if (version >= 4) {
            for (const auto& o : node->overrides) overrides.push_back({node->id, o.node, o.patch});
            node->overrides.clear();
        }
        if (!node->definition.empty() || !node->componentValues.empty()) {
            if (version < 3) return refused("component definitions and component values need snapshot version 3 or later");
            if (version < 4 && !node->definition.swaps.empty()) return refused("instance swap properties need snapshot version 4 or later");
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
    for(const auto& props:prototypeProps) ops.push_back(props);
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
