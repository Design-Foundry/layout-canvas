#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <string>
#include <vector>
#include "vc/diagnostics.hpp"
#include "vc/draw_list.hpp"
#include "vc/json.hpp"

using Catch::Matchers::WithinAbs;
using vc::CreateNode;
using vc::CreateToken;
using vc::Document;
using vc::Gradient;
using vc::GradientKind;
using vc::GradientStop;
using vc::Id;
using vc::Node;
using vc::NodePatch;
using vc::Oklch;
using vc::Paint;
using vc::SetProps;
using vc::Token;
using vc::TokenRef;
using vc::TokenType;

namespace {

Id idOf(std::uint64_t n) { return {5, n}; }
const vc::Rect kView{-1000, -1000, 4000, 4000};

Node rectNode(std::uint64_t id = 1) {
    Node n;
    n.id = idOf(id);
    n.order = "a0";
    n.w = 100;
    n.h = 60;
    return n;
}

Token colorToken(std::uint64_t id, const char* identifier, Oklch value) {
    Token token;
    token.id = idOf(id);
    token.identifier = identifier;
    token.value = value;
    return token;
}

Gradient twoStops() {
    Gradient g;
    g.angle = 90;
    g.stops = {{Oklch{0.9, 0.1, 30}, std::nullopt}, {Oklch{0.4, 0.1, 250}, 1.0}};
    return g;
}

NodePatch fillsPatch(std::vector<Paint> paints) {
    NodePatch patch;
    patch.fills = std::move(paints);
    return patch;
}

std::vector<vc::DrawItem> fillItems(const Document& doc) {
    std::vector<vc::DrawItem> items, fills;
    vc::buildDrawList(doc, kView, vc::kDefaultModeId, items);
    for (const auto& item : items) if (item.kind == vc::DrawKind::Fill) fills.push_back(item);
    return fills;
}

} // namespace

TEST_CASE("a node starts with one visible black solid paint and a patch replaces the whole stack") {
    Document doc;
    REQUIRE(doc.apply(CreateNode{rectNode()}));
    REQUIRE(doc.find(idOf(1))->fills == std::vector<Paint>{Paint{Oklch{}}});

    const std::vector<Paint> stack{Paint{Oklch{0.9, 0, 0}}, Paint{twoStops()}};
    const auto undo = doc.apply(SetProps{idOf(1), fillsPatch(stack)});
    REQUIRE(undo.has_value());
    REQUIRE(doc.find(idOf(1))->fills == stack);

    REQUIRE(doc.apply(*undo).has_value());
    REQUIRE(doc.find(idOf(1))->fills == std::vector<Paint>{Paint{Oklch{}}});
}

TEST_CASE("fills are validated: count, opacity, gradient shape, token families") {
    Document doc;
    REQUIRE(doc.apply(CreateNode{rectNode()}));
    doc.apply(CreateToken{colorToken(10, "ink", {0.2, 0, 0})});
    Token dimension;
    dimension.id = idOf(11);
    dimension.type = TokenType::Dimension;
    dimension.identifier = "gap";
    dimension.value = vc::Magnitude{4, vc::Unit::Px};
    REQUIRE(doc.apply(CreateToken{dimension}));

    vc::clearDiagnostic();
    REQUIRE_FALSE(doc.apply(SetProps{idOf(1), fillsPatch({})}));
    const auto empty = vc::lastDiagnostic();
    REQUIRE(empty.has_value());
    REQUIRE(empty->message.find("at least one paint") != std::string::npos);

    REQUIRE_FALSE(doc.apply(SetProps{idOf(1), fillsPatch(std::vector<Paint>(vc::kMaxFills + 1))}));
    REQUIRE(doc.apply(SetProps{idOf(1), fillsPatch(std::vector<Paint>(vc::kMaxFills))}));

    Paint faint{Oklch{0.5, 0, 0}};
    faint.opacity = 1.5;
    REQUIRE_FALSE(doc.apply(SetProps{idOf(1), fillsPatch({faint})}));
    faint.opacity = 0.5;
    REQUIRE(doc.apply(SetProps{idOf(1), fillsPatch({faint})}));

    Gradient oneStop = twoStops();
    oneStop.stops.pop_back();
    REQUIRE_FALSE(doc.apply(SetProps{idOf(1), fillsPatch({Paint{oneStop}})}));
    Gradient bad = twoStops();
    bad.shape = vc::RadialShape::Circle;
    REQUIRE_FALSE(doc.apply(SetProps{idOf(1), fillsPatch({Paint{bad}})}));
    Gradient outside = twoStops();
    outside.stops[1].position = 1.5;
    REQUIRE_FALSE(doc.apply(SetProps{idOf(1), fillsPatch({Paint{outside}})}));

    REQUIRE(doc.apply(SetProps{idOf(1), fillsPatch({Paint{TokenRef{idOf(10)}}})}));
    vc::clearDiagnostic();
    REQUIRE_FALSE(doc.apply(SetProps{idOf(1), fillsPatch({Paint{TokenRef{idOf(11)}}})}));
    const auto family = vc::lastDiagnostic();
    REQUIRE(family.has_value());
    REQUIRE(family->message.find("color token or a gradient token") != std::string::npos);
    REQUIRE_FALSE(doc.apply(SetProps{idOf(1), fillsPatch({Paint{TokenRef{idOf(99)}}})}));

    Gradient withTokenStop = twoStops();
    withTokenStop.stops[0].color = TokenRef{idOf(11)};
    REQUIRE_FALSE(doc.apply(SetProps{idOf(1), fillsPatch({Paint{withTokenStop}})}));
    withTokenStop.stops[0].color = TokenRef{idOf(10)};
    REQUIRE(doc.apply(SetProps{idOf(1), fillsPatch({Paint{withTokenStop}})}));
}

TEST_CASE("a paint can reference a gradient token, and the tokens it uses stay referenced") {
    Document doc;
    REQUIRE(doc.apply(CreateNode{rectNode()}));
    REQUIRE(doc.apply(CreateToken{colorToken(10, "a", {0.9, 0, 0})}));
    REQUIRE(doc.apply(CreateToken{colorToken(11, "b", {0.3, 0, 0})}));
    Token sky;
    sky.id = idOf(20);
    sky.type = TokenType::Gradient;
    sky.identifier = "sky";
    vc::GradientValue value;
    value.stops = {{idOf(10), std::nullopt, {}}, {idOf(11), std::nullopt, {}}};
    vc::normalizeGradientGeometry(value);
    sky.value = value;
    REQUIRE(doc.apply(CreateToken{sky}));

    REQUIRE(doc.apply(SetProps{idOf(1), fillsPatch({Paint{TokenRef{idOf(20)}}})}));
    REQUIRE(doc.isTokenReferenced(idOf(20)));
    REQUIRE_FALSE(doc.apply(vc::DeleteToken{idOf(20)}));

    Gradient literal = twoStops();
    literal.stops[1].color = TokenRef{idOf(11)};
    REQUIRE(doc.apply(SetProps{idOf(1), fillsPatch({Paint{literal}})}));
    REQUIRE(doc.isTokenReferenced(idOf(11)));
    REQUIRE_FALSE(doc.apply(vc::DeleteToken{idOf(11)}));
}

TEST_CASE("every visible solid paint of the stack draws, bottom first, with its own opacity") {
    Document doc;
    Node n = rectNode();
    Paint top{Oklch{0.9, 0, 0}};
    top.opacity = 0.5;
    Paint hidden{Oklch{0.2, 0, 0}};
    hidden.visible = false;
    n.fills = {Paint{Oklch{0.1, 0, 0}}, hidden, top, Paint{twoStops()}};
    REQUIRE(doc.apply(CreateNode{n}));

    const auto fills = fillItems(doc);
    REQUIRE(fills.size() == 2);
    REQUIRE_THAT(fills[0].color.a, WithinAbs(1.0, 1e-6));
    REQUIRE_THAT(fills[1].color.a, WithinAbs(0.5, 1e-6));
    REQUIRE(fills[0].color.r < fills[1].color.r);
}

TEST_CASE("fillVisible hides the whole stack and text takes the first visible solid paint") {
    Document doc;
    Node n = rectNode();
    n.fills = {Paint{Oklch{0.1, 0, 0}}, Paint{Oklch{0.9, 0, 0}}};
    n.fillVisible = false;
    REQUIRE(doc.apply(CreateNode{n}));
    REQUIRE(fillItems(doc).empty());

    Document gradientOnly;
    Node g = rectNode();
    g.fills = {Paint{twoStops()}};
    REQUIRE(gradientOnly.apply(CreateNode{g}));
    REQUIRE(fillItems(gradientOnly).empty());
}

TEST_CASE("a single plain solid paint keeps the legacy fill on the wire and the old snapshot version") {
    Document doc;
    Node n = rectNode();
    n.fills = {Paint{Oklch{0.5, 0.1, 100}}};
    REQUIRE(doc.apply(CreateNode{n}));
    const std::string json = vc::snapshotToJson(doc);
    REQUIRE(json.find("\"fill\":{") != std::string::npos);
    REQUIRE(json.find("\"fills\"") == std::string::npos);
    REQUIRE(json.find("\"version\":2}") != std::string::npos);
}

TEST_CASE("stacks, gradients and paint options need snapshot 30 and round-trip") {
    Document doc;
    Node n = rectNode();
    Paint dim{TokenRef{idOf(40)}};
    dim.opacity = 0.25;
    Paint off{Oklch{0.3, 0.1, 10}};
    off.visible = false;
    Gradient g = twoStops();
    g.geometry = GradientKind::Conic;
    g.angle = 45;
    g.centerX = 30;
    g.centerY = 70;
    g.repeating = true;
    n.fills = {off, Paint{g}};
    REQUIRE(doc.apply(CreateNode{n}));
    doc.apply(CreateToken{colorToken(40, "ink", {0.2, 0, 0})});
    Node second = rectNode(2);
    second.order = "a1";
    second.fills = {dim, Paint{Oklch{0.6, 0.1, 80}}};
    REQUIRE(doc.apply(CreateNode{second}));

    const std::string json = vc::snapshotToJson(doc);
    REQUIRE(json.find("\"version\":30") != std::string::npos);
    REQUIRE(json.find("\"fills\"") != std::string::npos);
    const auto ops = vc::snapshotFromJson(json);
    REQUIRE(ops.has_value());
    Document loaded;
    for (const vc::OpBody& op : *ops) REQUIRE(loaded.apply(op));
    REQUIRE(loaded.find(idOf(1))->fills == n.fills);
    REQUIRE(loaded.find(idOf(2))->fills == second.fills);
    REQUIRE(vc::snapshotToJson(loaded) == json);

    std::string older = json;
    older.replace(older.find("\"version\":30"), 12, "\"version\":29");
    vc::clearDiagnostic();
    REQUIRE_FALSE(vc::snapshotFromJson(older).has_value());
    const auto gate = vc::lastDiagnostic();
    REQUIRE(gate.has_value());
    REQUIRE(gate->message == "fills need snapshot version 30 or later");
}

TEST_CASE("fill and fills are alternatives and a paint takes exactly one source") {
    const auto parse = [](const std::string& body) {
        return vc::patchFromJson(body);
    };
    REQUIRE(parse(R"({"fills":[{"color":{"l":0.5,"c":0,"h":0,"a":1}}]})").has_value());
    REQUIRE(parse(R"({"fills":[{"gradient":{"geometry":"linear","stops":[{"color":{"l":0.5,"c":0,"h":0,"a":1}},{"color":{"l":0.9,"c":0,"h":0,"a":1},"position":1}]},"opacity":0.5,"visible":false}]})").has_value());
    REQUIRE_FALSE(parse(R"({"fill":{"l":0.5,"c":0,"h":0,"a":1},"fills":[]})").has_value());
    REQUIRE_FALSE(parse(R"({"fills":[{}]})").has_value());
    REQUIRE_FALSE(parse(R"({"fills":[{"color":{"l":0.5,"c":0,"h":0,"a":1},"token":"00000000-0000-0000-0000-000000000001"}]})").has_value());
    REQUIRE_FALSE(parse(R"({"fills":[{"color":{"l":0.5,"c":0,"h":0,"a":1},"blend":"multiply"}]})").has_value());
    REQUIRE_FALSE(parse(R"({"fills":[{"gradient":{"stops":[]}}]})").has_value());
    REQUIRE_FALSE(parse(R"({"fills":{"color":{}}})").has_value());

    const auto legacy = parse(R"({"fill":{"l":0.5,"c":0,"h":0,"a":1}})");
    REQUIRE(legacy.has_value());
    REQUIRE(legacy->fills.has_value());
    REQUIRE(legacy->fills->size() == 1);
    REQUIRE(std::holds_alternative<Oklch>(legacy->fills->front().source));
}

TEST_CASE("the inspector keeps a fill color for a stack and also lists the paints") {
    Document doc;
    Node n = rectNode();
    n.fills = {Paint{twoStops()}, Paint{Oklch{0.5, 0.1, 100}}};
    REQUIRE(doc.apply(CreateNode{n}));
    const std::string json = vc::inspectNodeJson(doc, *doc.find(idOf(1)), vc::kDefaultModeId);
    REQUIRE(json.find("\"fills\"") != std::string::npos);
    REQUIRE(json.find("\"fill\":{") != std::string::npos);
    REQUIRE(json.find("\"hex\"") != std::string::npos);
}
