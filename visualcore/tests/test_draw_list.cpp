#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "vc/draw_list.hpp"

using Catch::Matchers::WithinAbs;
using vc::CreateNode;
using vc::Document;
using vc::DrawItem;
using vc::DrawKind;
using vc::Id;
using vc::Node;
using vc::NodeType;
using vc::Rect;

namespace {

Id idOf(std::uint64_t n) {
    return {4, n};
}

Node rect(std::uint64_t id, double x, double y, double w, double h, std::uint64_t parent = 0) {
    Node n;
    n.id = idOf(id);
    n.parent = parent ? idOf(parent) : Id{};
    n.order = "a" + std::to_string(id % 10);
    n.x = x;
    n.y = y;
    n.w = w;
    n.h = h;
    return n;
}

const Rect kView{-1000, -1000, 4000, 4000};

std::vector<DrawItem> draw(const Document& doc, const Rect& view = kView) {
    std::vector<DrawItem> items;
    vc::buildDrawList(doc, view, vc::kDefaultModeId, items);
    return items;
}

bool same(const Rect& a, const Rect& b) {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

} // namespace

TEST_CASE("paint order is drop shadows, fill, inner shadows, children, then stroke") {
    Document doc;
    Node frame = rect(1, 0, 0, 100, 100);
    frame.type = NodeType::Frame;
    vc::Stroke stroke;
    stroke.width = 2.0;
    frame.stroke = stroke;
    frame.shadows = std::vector<vc::Shadow>{{0, 4, 8, 0, vc::Oklch{0, 0, 0, 0.3}, false, true},
                                            {0, 1, 2, 0, vc::Oklch{0, 0, 0, 0.3}, true, true}};
    doc.apply(CreateNode{frame});
    doc.apply(CreateNode{rect(2, 10, 10, 20, 20, 1)});

    const auto items = draw(doc);
    REQUIRE(items.size() == 5);
    REQUIRE(items[0].kind == DrawKind::DropShadow);
    REQUIRE(items[1].kind == DrawKind::Fill);
    REQUIRE(items[2].kind == DrawKind::InnerShadow);
    REQUIRE(items[3].kind == DrawKind::Fill);
    REQUIRE(same(items[3].a.rect, {10, 10, 20, 20}));
    REQUIRE(items[4].kind == DrawKind::Stroke);
}

TEST_CASE("stroke alignment moves the stroke band relative to the box") {
    const auto band = [](vc::StrokeAlign align) {
        Document doc;
        Node n = rect(1, 0, 0, 100, 50);
        vc::Stroke stroke;
        stroke.width = 4.0;
        stroke.align = align;
        n.stroke = stroke;
        doc.apply(CreateNode{n});
        const auto items = draw(doc);
        return std::pair{items.back().a.rect, items.back().b.rect};
    };
    const auto [insideOuter, insideInner] = band(vc::StrokeAlign::Inside);
    REQUIRE(same(insideOuter, {0, 0, 100, 50}));
    REQUIRE(same(insideInner, {4, 4, 92, 42}));

    const auto [centerOuter, centerInner] = band(vc::StrokeAlign::Center);
    REQUIRE(same(centerOuter, {-2, -2, 104, 54}));
    REQUIRE(same(centerInner, {2, 2, 96, 46}));

    const auto [outsideOuter, outsideInner] = band(vc::StrokeAlign::Outside);
    REQUIRE(same(outsideOuter, {-4, -4, 108, 58}));
    REQUIRE(same(outsideInner, {0, 0, 100, 50}));
}

TEST_CASE("per-side stroke widths inset the inner box per side") {
    Document doc;
    Node n = rect(1, 0, 0, 100, 50);
    vc::Stroke stroke;
    stroke.width = 1.0;
    stroke.sides[vc::kBottom] = 3.0;
    n.stroke = stroke;
    doc.apply(CreateNode{n});

    REQUIRE(same(draw(doc).back().b.rect, {1, 1, 98, 46}));
}

TEST_CASE("drop shadows offset, spread and reach three sigmas") {
    Document doc;
    Node n = rect(1, 0, 0, 100, 50);
    n.cornerRadius = 8.0;
    n.shadows = std::vector<vc::Shadow>{{2, 4, 10, 3, vc::Oklch{0, 0, 0, 0.5}, false, true}};
    doc.apply(CreateNode{n});

    const DrawItem shadow = draw(doc).front();
    REQUIRE(same(shadow.a.rect, {-1, 1, 106, 56}));
    REQUIRE(shadow.a.radii[0] == 11);
    REQUIRE(shadow.sigma == 5);
    REQUIRE(same(shadow.quad, {-16, -14, 136, 86}));
}

TEST_CASE("hidden fills, hidden shadows and opacity") {
    Document doc;
    Node n = rect(1, 0, 0, 10, 10);
    n.fillVisible = false;
    n.shadows = std::vector<vc::Shadow>{{0, 0, 0, 0, vc::Oklch{0, 0, 0, 1}, false, false}};
    doc.apply(CreateNode{n});
    REQUIRE(draw(doc).empty());

    Node parent = rect(2, 0, 0, 50, 50);
    parent.type = NodeType::Frame;
    parent.opacity = 0.5;
    Node child = rect(3, 0, 0, 10, 10, 2);
    child.fill = vc::Oklch{0, 0, 0, 1};
    child.opacity = 0.5;
    doc.apply(CreateNode{parent});
    doc.apply(CreateNode{child});
    REQUIRE_THAT(draw(doc).back().color.a, WithinAbs(0.25, 1e-6));
}

TEST_CASE("nodes outside the view are skipped unless their shadow reaches in") {
    Document doc;
    doc.apply(CreateNode{rect(1, 500, 0, 10, 10)});
    Node shadowed = rect(2, 520, 0, 10, 10);
    shadowed.shadows = std::vector<vc::Shadow>{{-30, 0, 0, 0, vc::Oklch{0, 0, 0, 1}, false, true}};
    doc.apply(CreateNode{shadowed});

    const auto items = draw(doc, {0, 0, 495, 100});
    REQUIRE(items.size() == 2);
    REQUIRE(items[0].kind == DrawKind::DropShadow);
}

TEST_CASE("clipping frames pass their box to children") {
    Document doc;
    Node frame = rect(1, 10, 10, 50, 50);
    frame.type = NodeType::Frame;
    frame.clipContent = true;
    doc.apply(CreateNode{frame});
    doc.apply(CreateNode{rect(2, 40, 40, 30, 30, 1)});

    const auto items = draw(doc);
    REQUIRE(same(items.back().clip, {10, 10, 50, 50}));
    REQUIRE(same(items.back().a.rect, {50, 50, 30, 30}));
}
