#include <catch2/catch_test_macros.hpp>
#include "vc/document.hpp"
#include "vc/editor.hpp"
#include "vc/geometry.hpp"
#include "vc/json.hpp"

using vc::Align;
using vc::CreateNode;
using vc::Document;
using vc::Id;
using vc::Justify;
using vc::LayoutType;
using vc::Node;
using vc::NodeType;
using vc::Rect;
using vc::Sizing;

namespace {

Id idOf(std::uint64_t n) {
    return {3, n};
}

Node frame(std::uint64_t id, LayoutType type, double w = 300, double h = 100) {
    Node n;
    n.id = idOf(id);
    n.type = NodeType::Frame;
    n.order = "a0";
    n.w = w;
    n.h = h;
    n.layout.type = type;
    return n;
}

Node child(std::uint64_t id, std::uint64_t parent, const char* order, double w, double h) {
    Node n;
    n.id = idOf(id);
    n.parent = idOf(parent);
    n.order = order;
    n.w = w;
    n.h = h;
    return n;
}

bool same(const Rect& a, const Rect& b) {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

} // namespace

TEST_CASE("a row places children in order with padding and gap") {
    Document doc;
    Node f = frame(1, LayoutType::Row);
    f.layout.gap = 10.0;
    f.layout.padding = {5.0, 6.0, 7.0, 8.0};
    doc.apply(CreateNode{f});
    doc.apply(CreateNode{child(2, 1, "a0", 40, 20)});
    doc.apply(CreateNode{child(3, 1, "a1", 30, 50)});

    REQUIRE(same(doc.box(idOf(2)), {8, 5, 40, 20}));
    REQUIRE(same(doc.box(idOf(3)), {58, 5, 30, 50}));
}

TEST_CASE("hug sizes a frame to its content plus padding") {
    Document doc;
    Node f = frame(1, LayoutType::Column);
    f.widthSizing = Sizing::Hug;
    f.heightSizing = Sizing::Hug;
    f.layout.gap = 4.0;
    f.layout.padding = {10.0, 10.0, 10.0, 10.0};
    doc.apply(CreateNode{f});
    doc.apply(CreateNode{child(2, 1, "a0", 40, 20)});
    doc.apply(CreateNode{child(3, 1, "a1", 60, 30)});

    const Rect box = doc.box(idOf(1));
    REQUIRE(box.w == 80);
    REQUIRE(box.h == 74);
}

TEST_CASE("fill children share the space left after fixed ones and gaps") {
    Document doc;
    Node f = frame(1, LayoutType::Row, 300, 100);
    f.layout.gap = 10.0;
    doc.apply(CreateNode{f});
    doc.apply(CreateNode{child(2, 1, "a0", 50, 20)});
    Node a = child(3, 1, "a1", 1, 20);
    a.widthSizing = Sizing::Fill;
    Node b = child(4, 1, "a2", 1, 20);
    b.widthSizing = Sizing::Fill;
    doc.apply(CreateNode{a});
    doc.apply(CreateNode{b});

    REQUIRE(doc.box(idOf(3)).w == 115);
    REQUIRE(doc.box(idOf(4)).x == 50 + 10 + 115 + 10);
}

TEST_CASE("min and max clamp fill and hug sizes") {
    Document doc;
    doc.apply(CreateNode{frame(1, LayoutType::Row, 300, 100)});
    Node filled = child(2, 1, "a0", 1, 20);
    filled.widthSizing = Sizing::Fill;
    filled.limits.maxWidth = 120.0;
    doc.apply(CreateNode{filled});

    REQUIRE(doc.box(idOf(2)).w == 120);

    Node hug = frame(3, LayoutType::Row);
    hug.widthSizing = Sizing::Hug;
    hug.limits.minWidth = 200.0;
    doc.apply(CreateNode{hug});
    REQUIRE(doc.box(idOf(3)).w == 200);
}

TEST_CASE("justification distributes free space along the main axis") {
    const auto xs = [](Justify justify) {
        Document doc;
        Node f = frame(1, LayoutType::Row, 300, 100);
        f.layout.justify = justify;
        doc.apply(CreateNode{f});
        doc.apply(CreateNode{child(2, 1, "a0", 50, 20)});
        doc.apply(CreateNode{child(3, 1, "a1", 50, 20)});
        return std::pair{doc.box(idOf(2)).x, doc.box(idOf(3)).x};
    };
    REQUIRE(xs(Justify::Start) == std::pair{0.0, 50.0});
    REQUIRE(xs(Justify::Center) == std::pair{100.0, 150.0});
    REQUIRE(xs(Justify::End) == std::pair{200.0, 250.0});
    REQUIRE(xs(Justify::SpaceBetween) == std::pair{0.0, 250.0});
    REQUIRE(xs(Justify::SpaceAround) == std::pair{50.0, 200.0});
    REQUIRE(xs(Justify::SpaceEvenly) == std::pair{200.0 / 3.0, 50.0 + 400.0 / 3.0});
}

TEST_CASE("alignment positions or stretches children across the cross axis") {
    const auto box = [](Align align, Sizing cross = Sizing::Fixed) {
        Document doc;
        Node f = frame(1, LayoutType::Row, 300, 100);
        f.layout.align = align;
        doc.apply(CreateNode{f});
        Node c = child(2, 1, "a0", 50, 20);
        c.heightSizing = cross;
        doc.apply(CreateNode{c});
        return doc.box(idOf(2));
    };
    REQUIRE(box(Align::Start).y == 0);
    REQUIRE(box(Align::Center).y == 40);
    REQUIRE(box(Align::End).y == 80);
    REQUIRE(box(Align::Stretch).h == 100);
    REQUIRE(box(Align::Center, Sizing::Fill).h == 100);
}

TEST_CASE("absolute and hidden children stay out of the flow") {
    Document doc;
    doc.apply(CreateNode{frame(1, LayoutType::Row)});
    Node pinned = child(2, 1, "a0", 40, 20);
    pinned.absolute = true;
    pinned.x = 200;
    pinned.y = 60;
    Node hidden = child(3, 1, "a1", 40, 20);
    hidden.visible = false;
    doc.apply(CreateNode{pinned});
    doc.apply(CreateNode{hidden});
    doc.apply(CreateNode{child(4, 1, "a2", 40, 20)});

    REQUIRE(same(doc.box(idOf(2)), {200, 60, 40, 20}));
    REQUIRE(doc.box(idOf(4)).x == 0);
}

TEST_CASE("layout follows edits and nested hug frames") {
    Document doc;
    Node outer = frame(1, LayoutType::Column);
    outer.widthSizing = Sizing::Hug;
    outer.heightSizing = Sizing::Hug;
    doc.apply(CreateNode{outer});
    Node inner = frame(2, LayoutType::Row);
    inner.parent = idOf(1);
    inner.widthSizing = Sizing::Hug;
    inner.heightSizing = Sizing::Hug;
    doc.apply(CreateNode{inner});
    doc.apply(CreateNode{child(3, 2, "a0", 40, 20)});
    REQUIRE(doc.box(idOf(1)).w == 40);

    vc::NodePatch wider;
    wider.w = 90.0;
    doc.apply(vc::SetProps{idOf(3), wider});
    REQUIRE(doc.box(idOf(1)).w == 90);
}

TEST_CASE("deeply nested fixed-width hug-height rows measure each child once") {
    Document doc;
    constexpr std::uint64_t depth = 40;
    for (std::uint64_t i = 1; i <= depth; ++i) {
        Node row = frame(i, LayoutType::Row, 200);
        row.heightSizing = Sizing::Hug;
        if (i > 1) row.parent = idOf(i - 1);
        doc.apply(CreateNode{row});
    }
    doc.apply(CreateNode{child(depth + 1, depth, "a0", 20, 10)});

    REQUIRE(doc.box(idOf(1)).h == 10);
}

TEST_CASE("gaps and paddings resolve through tokens in the active mode") {
    Document doc;
    const Id dark{9, 9};
    doc.apply(vc::CreateMode{{dark, "Dark", "a1"}});
    vc::Token small;
    small.id = idOf(50);
    small.type = vc::TokenType::Dimension;
    small.identifier = "space-1";
    small.value = vc::Magnitude{4, vc::Unit::Px};
    vc::Token large = small;
    large.id = idOf(51);
    large.identifier = "space-4";
    large.value = vc::Magnitude{1, vc::Unit::Rem};
    vc::Token gap;
    gap.id = idOf(52);
    gap.type = vc::TokenType::Dimension;
    gap.identifier = "gap";
    gap.source = {{vc::kDefaultModeId, idOf(50)}, {dark, idOf(51)}};
    doc.apply(vc::CreateToken{small});
    doc.apply(vc::CreateToken{large});
    doc.apply(vc::CreateToken{gap});

    Node f = frame(1, LayoutType::Row);
    f.layout.gap = vc::TokenRef{idOf(52)};
    doc.apply(CreateNode{f});
    doc.apply(CreateNode{child(2, 1, "a0", 40, 20)});
    doc.apply(CreateNode{child(3, 1, "a1", 40, 20)});

    REQUIRE(doc.box(idOf(3)).x == 44);
    REQUIRE(doc.box(idOf(3), dark).x == 56);
}

TEST_CASE("dragging inside an auto-layout frame reorders instead of moving") {
    vc::Session session;
    vc::Camera camera;
    vc::Editor editor{session, camera};
    session.edit(CreateNode{frame(1, LayoutType::Row, 300, 100)});
    session.edit(CreateNode{child(2, 1, "a0", 50, 50)});
    session.edit(CreateNode{child(3, 1, "a1", 50, 50)});
    session.edit(CreateNode{child(4, 1, "a2", 50, 50)});

    editor.pointerDown({25, 25}, vc::PointerButton::Primary);
    editor.pointerMove({90, 25});
    editor.pointerMove({140, 25});
    editor.pointerUp();

    const auto& order = session.document().children(idOf(1));
    REQUIRE(order == std::vector<Id>{idOf(3), idOf(4), idOf(2)});
    REQUIRE(session.document().box(idOf(2)).x == 100);
}

TEST_CASE("resizing a hug or fill axis switches it to fixed") {
    vc::Session session;
    vc::Camera camera;
    vc::Editor editor{session, camera};
    Node f = frame(1, LayoutType::Row);
    f.widthSizing = Sizing::Hug;
    f.heightSizing = Sizing::Hug;
    session.edit(CreateNode{f});
    session.edit(CreateNode{child(2, 1, "a0", 50, 50)});
    editor.select(idOf(1));

    editor.pointerDown({50, 25}, vc::PointerButton::Primary);
    editor.pointerMove({80, 25});
    editor.pointerUp();

    const Node& resized = *session.document().find(idOf(1));
    REQUIRE(resized.widthSizing == Sizing::Fixed);
    REQUIRE(resized.heightSizing == Sizing::Hug);
    REQUIRE(session.document().box(idOf(1)).w == 80);
}

TEST_CASE("every node property survives a json round trip") {
    Node n = frame(1, LayoutType::Column);
    n.fillVisible = false;
    n.cornerRadii = {std::nullopt, vc::Value<double>{4.0}, std::nullopt, vc::Value<double>{vc::TokenRef{idOf(9)}}};
    vc::Stroke stroke;
    stroke.width = 2.0;
    stroke.sides[vc::kBottom] = 4.0;
    stroke.style = vc::StrokeStyle::Dashed;
    stroke.align = vc::StrokeAlign::Outside;
    n.stroke = stroke;
    n.shadows = std::vector<vc::Shadow>{{1, 2, 3, 4, vc::Oklch{0, 0, 0, 0.5}, true, false}};
    n.opacity = vc::TokenRef{idOf(8)};
    n.locked = true;
    n.layout.justify = Justify::SpaceBetween;
    n.layout.align = Align::Stretch;
    n.layout.padding = {1.0, 2.0, 3.0, vc::TokenRef{idOf(7)}};
    n.widthSizing = Sizing::Hug;
    n.heightSizing = Sizing::Fill;
    n.limits.minWidth = 10.0;
    n.limits.maxHeight = vc::TokenRef{idOf(6)};
    n.absolute = true;

    const auto parsed = vc::opFromJson("CreateNode", vc::opToJson(CreateNode{n}));
    REQUIRE(parsed.has_value());
    REQUIRE(std::get<CreateNode>(*parsed).node == n);

    Node border = n;
    border.stroke = vc::TokenRef{idOf(5)};
    border.shadows = vc::TokenRef{idOf(4)};
    const auto parsedRefs = vc::opFromJson("CreateNode", vc::opToJson(CreateNode{border}));
    REQUIRE(std::get<CreateNode>(*parsedRefs).node == border);
}
