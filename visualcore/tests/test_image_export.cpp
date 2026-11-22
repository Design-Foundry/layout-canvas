#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include "vc/image_export.hpp"

using Catch::Matchers::WithinAbs;
using vc::CreateNode;
using vc::Document;
using vc::Id;
using vc::Node;
using vc::NodeType;
using vc::Rect;
using vc::Shadow;
using vc::Stroke;

namespace {

Node frame(std::uint64_t id, double x, double y, double w, double h, std::uint64_t parent = 0) {
    Node n;
    n.id = {4, id};
    n.parent = parent ? Id{4, parent} : Id{};
    n.order = "a" + std::to_string(id % 10);
    n.type = NodeType::Frame;
    n.x = x;
    n.y = y;
    n.w = w;
    n.h = h;
    return n;
}

} // namespace

TEST_CASE("Exports at 1x, 2x and 3x size the image from the node bounds", "[export]") {
    for (const double scale : {1.0, 2.0, 3.0}) {
        const auto plan = vc::planExport({10, 20, 120, 80}, scale, 8192);
        REQUIRE(plan);
        REQUIRE(plan->width == std::uint32_t(120 * scale));
        REQUIRE(plan->height == std::uint32_t(80 * scale));
        REQUIRE_FALSE(plan->clamped);
        REQUIRE_FALSE(plan->tiled());
        REQUIRE(plan->scale == scale);
    }
}

TEST_CASE("Fractional bounds round the image up so no content is lost", "[export]") {
    const auto plan = vc::planExport({0, 0, 10.2, 10.2}, 2.0, 8192);
    REQUIRE(plan->width == 21);
    REQUIRE(plan->height == 21);
}

TEST_CASE("Empty bounds and invalid scales have no plan", "[export]") {
    REQUIRE_FALSE(vc::planExport({0, 0, 0, 10}, 1.0, 8192));
    REQUIRE_FALSE(vc::planExport({0, 0, 10, 10}, 0.0, 8192));
    REQUIRE_FALSE(vc::planExport({0, 0, 10, 10}, 1.0, 0));
}

TEST_CASE("Targets beyond the texture limit are tiled and the tiles cover the image exactly", "[export]") {
    const auto plan = vc::planExport({100, 200, 5000, 3000}, 1.0, 2048);
    REQUIRE(plan);
    REQUIRE(plan->tiled());
    REQUIRE_FALSE(plan->clamped);
    REQUIRE(plan->tileSize == 2048);
    REQUIRE(plan->columns == 3);
    REQUIRE(plan->rows == 2);
    std::uint64_t covered = 0;
    for (std::uint32_t row = 0; row < plan->rows; ++row)
        for (std::uint32_t column = 0; column < plan->columns; ++column) {
            const auto tile = vc::exportTilePixels(*plan, column, row);
            REQUIRE(tile.w <= 2048);
            REQUIRE(tile.h <= 2048);
            covered += std::uint64_t(tile.w) * tile.h;
        }
    REQUIRE(covered == 5000ull * 3000ull);
    const auto last = vc::exportTileWorld(*plan, {100, 200, 5000, 3000}, 2, 1);
    REQUIRE_THAT(last.x + last.w, WithinAbs(5100, 1e-6));
    REQUIRE_THAT(last.y + last.h, WithinAbs(3200, 1e-6));
}

TEST_CASE("Tiles never exceed the fixed tile size even on large textures", "[export]") {
    const auto plan = vc::planExport({0, 0, 6000, 100}, 1.0, 16384);
    REQUIRE(plan->tileSize == vc::kMaxExportTile);
    REQUIRE(plan->columns == 2);
}

TEST_CASE("Images beyond the size limits clamp the scale instead", "[export]") {
    const auto side = vc::planExport({0, 0, 10000, 100}, 3.0, 8192);
    REQUIRE(side->clamped);
    REQUIRE(side->requestedScale == 3.0);
    REQUIRE(side->scale < 2.0);
    REQUIRE(side->width <= vc::kMaxExportSide);

    const auto area = vc::planExport({0, 0, 10000, 10000}, 3.0, 8192);
    REQUIRE(area->clamped);
    REQUIRE(std::uint64_t(area->width) * area->height <= vc::kMaxExportPixels);
    REQUIRE(area->width <= vc::kMaxExportSide);
    REQUIRE(area->tiled());
}

TEST_CASE("The export draw list holds only the node subtree", "[export]") {
    Document doc;
    auto target = frame(1, 100, 100, 200, 100);
    REQUIRE(doc.apply(CreateNode{target}));
    REQUIRE(doc.apply(CreateNode{frame(2, 50, 20, 100, 40, 1)}));
    REQUIRE(doc.apply(CreateNode{frame(3, 90, 90, 300, 300)}));
    REQUIRE(doc.apply(CreateNode{frame(4, 500, 500, 10, 10)}));

    std::vector<vc::DrawItem> items;
    REQUIRE(vc::buildExportDrawList(doc, target.id, {-1000, -1000, 4000, 4000}, vc::kDefaultModeId, items));
    REQUIRE_FALSE(items.empty());
    for (const auto& item : items) REQUIRE((item.node == target.id || item.node == Id{4, 2}));
}

TEST_CASE("A nested node exports without its siblings, its parent or the parent's clip", "[export]") {
    Document doc;
    auto parent = frame(1, 0, 0, 100, 100);
    parent.clipContent = true;
    REQUIRE(doc.apply(CreateNode{parent}));
    REQUIRE(doc.apply(CreateNode{frame(2, 60, 60, 100, 100, 1)}));
    REQUIRE(doc.apply(CreateNode{frame(3, 0, 0, 20, 20, 1)}));
    std::vector<vc::DrawItem> items;
    REQUIRE(vc::buildExportDrawList(doc, Id{4, 2}, {0, 0, 400, 400}, vc::kDefaultModeId, items));
    REQUIRE_FALSE(items.empty());
    for (const auto& item : items) REQUIRE(item.node == Id{4, 2});
    const auto bounds = vc::exportBounds(doc, Id{4, 2}, vc::kDefaultModeId);
    REQUIRE(bounds);
    REQUIRE(bounds->x == 60);
    REQUIRE(bounds->w == 100);
}

TEST_CASE("Tiles outside the view contribute no paint", "[export]") {
    Document doc;
    REQUIRE(doc.apply(CreateNode{frame(1, 0, 0, 100, 100)}));
    std::vector<vc::DrawItem> items;
    REQUIRE(vc::buildExportDrawList(doc, Id{4, 1}, {500, 500, 50, 50}, vc::kDefaultModeId, items));
    REQUIRE(items.empty());
}

TEST_CASE("Hidden or missing nodes cannot be exported", "[export]") {
    Document doc;
    auto hidden = frame(1, 0, 0, 100, 100);
    hidden.visible = false;
    REQUIRE(doc.apply(CreateNode{hidden}));
    std::vector<vc::DrawItem> items;
    REQUIRE_FALSE(vc::buildExportDrawList(doc, hidden.id, {0, 0, 100, 100}, vc::kDefaultModeId, items));
    REQUIRE_FALSE(vc::buildExportDrawList(doc, Id{4, 99}, {0, 0, 100, 100}, vc::kDefaultModeId, items));
    REQUIRE_FALSE(vc::exportBounds(doc, hidden.id, vc::kDefaultModeId));
    REQUIRE_FALSE(vc::exportBounds(doc, Id{4, 99}, vc::kDefaultModeId));
}

TEST_CASE("Export bounds equal the box when nothing paints outside it", "[export]") {
    Document doc;
    REQUIRE(doc.apply(CreateNode{frame(1, 10.5, 20.5, 100, 50)}));
    const auto bounds = vc::exportBounds(doc, Id{4, 1}, vc::kDefaultModeId);
    REQUIRE(bounds);
    REQUIRE_THAT(bounds->x, WithinAbs(10.5, 1e-9));
    REQUIRE_THAT(bounds->y, WithinAbs(20.5, 1e-9));
    REQUIRE_THAT(bounds->w, WithinAbs(100, 1e-9));
    REQUIRE_THAT(bounds->h, WithinAbs(50, 1e-9));
}

TEST_CASE("Export bounds grow to include drop shadows", "[export]") {
    Document doc;
    auto node = frame(1, 100, 100, 100, 50);
    Shadow shadow;
    shadow.x = 10;
    shadow.y = 20;
    shadow.blur = 16;
    shadow.spread = 4;
    node.shadows = std::vector<Shadow>{shadow};
    REQUIRE(doc.apply(CreateNode{node}));
    const auto bounds = vc::exportBounds(doc, node.id, vc::kDefaultModeId);
    REQUIRE(bounds);
    REQUIRE(bounds->x <= 100 + 10 - 4 - 3 * 8 + 1e-6);
    REQUIRE(bounds->y <= 100 + 20 - 4 - 3 * 8 + 1e-6);
    REQUIRE(bounds->x + bounds->w >= 200 + 10 + 4 + 3 * 8 - 1e-6);
    REQUIRE(bounds->y + bounds->h >= 150 + 20 + 4 + 3 * 8 - 1e-6);
    REQUIRE(bounds->x == std::floor(bounds->x));
    REQUIRE(bounds->w == std::floor(bounds->w));
}

TEST_CASE("Inner shadows and inside strokes stay within the box", "[export]") {
    Document doc;
    auto node = frame(1, 100, 100, 100, 50);
    Shadow shadow;
    shadow.inset = true;
    shadow.blur = 10;
    node.shadows = std::vector<Shadow>{shadow};
    Stroke stroke;
    stroke.align = vc::StrokeAlign::Inside;
    stroke.width = 4.0;
    node.stroke = stroke;
    REQUIRE(doc.apply(CreateNode{node}));
    const auto bounds = vc::exportBounds(doc, node.id, vc::kDefaultModeId);
    REQUIRE(bounds);
    REQUIRE(bounds->x == 100);
    REQUIRE(bounds->w == 100);
    REQUIRE(bounds->h == 50);
}

TEST_CASE("Outside and center strokes grow the export bounds", "[export]") {
    for (const auto align : {vc::StrokeAlign::Outside, vc::StrokeAlign::Center}) {
        Document doc;
        auto node = frame(1, 100, 100, 100, 50);
        Stroke stroke;
        stroke.align = align;
        stroke.width = 6.0;
        node.stroke = stroke;
        REQUIRE(doc.apply(CreateNode{node}));
        const double grow = align == vc::StrokeAlign::Outside ? 6 : 3;
        const auto bounds = vc::exportBounds(doc, node.id, vc::kDefaultModeId);
        REQUIRE(bounds);
        REQUIRE_THAT(bounds->x, WithinAbs(100 - grow, 1e-9));
        REQUIRE_THAT(bounds->w, WithinAbs(100 + 2 * grow, 1e-9));
        REQUIRE_THAT(bounds->h, WithinAbs(50 + 2 * grow, 1e-9));
    }
}

TEST_CASE("Children overflowing an unclipped frame are exported, clipped ones are cut", "[export]") {
    for (const bool clip : {false, true}) {
        Document doc;
        auto parent = frame(1, 100, 100, 100, 100);
        parent.clipContent = clip;
        REQUIRE(doc.apply(CreateNode{parent}));
        REQUIRE(doc.apply(CreateNode{frame(2, 50, 50, 100, 100, 1)}));
        const auto bounds = vc::exportBounds(doc, parent.id, vc::kDefaultModeId);
        REQUIRE(bounds);
        REQUIRE(bounds->x + bounds->w == (clip ? 200 : 250));
        REQUIRE(bounds->y + bounds->h == (clip ? 200 : 250));
    }
}

TEST_CASE("A shadow on a child shows past a clipping parent only when the parent does not clip", "[export]") {
    Document doc;
    auto parent = frame(1, 100, 100, 100, 100);
    parent.clipContent = true;
    REQUIRE(doc.apply(CreateNode{parent}));
    auto child = frame(2, 10, 10, 50, 50, 1);
    Shadow shadow;
    shadow.blur = 20;
    child.shadows = std::vector<Shadow>{shadow};
    REQUIRE(doc.apply(CreateNode{child}));
    REQUIRE(vc::exportBounds(doc, parent.id, vc::kDefaultModeId)->x == 100);
    const auto own = vc::exportBounds(doc, child.id, vc::kDefaultModeId);
    REQUIRE(own);
    REQUIRE(own->x < 110);
}

TEST_CASE("A layer inside an instance exports through its expanded id", "[export]") {
    Document doc;
    Node component = frame(1, 400, 0, 100, 100);
    component.type = NodeType::Component;
    REQUIRE(doc.apply(CreateNode{component}));
    Node part = frame(2, 10, 10, 30, 30, 1);
    Shadow shadow;
    shadow.blur = 12;
    part.shadows = std::vector<Shadow>{shadow};
    REQUIRE(doc.apply(CreateNode{part}));
    Node instance = frame(3, 50, 60, 100, 100);
    instance.type = NodeType::Instance;
    instance.component = component.id;
    REQUIRE(doc.apply(CreateNode{instance}));

    const Id expanded = doc.viewChildren(instance.id).front();
    REQUIRE(expanded != part.id);
    const auto bounds = vc::exportBounds(doc, expanded, vc::kDefaultModeId);
    REQUIRE(bounds);
    REQUIRE(bounds->x < 60);
    REQUIRE(bounds->x + bounds->w > 90);
    REQUIRE(bounds->y < 70);

    std::vector<vc::DrawItem> items;
    REQUIRE(vc::buildExportDrawList(doc, expanded, {-1000, -1000, 4000, 4000}, vc::kDefaultModeId, items));
    REQUIRE_FALSE(items.empty());
    for (const auto& item : items) REQUIRE(item.node == expanded);

    const auto whole = vc::exportBounds(doc, instance.id, vc::kDefaultModeId);
    REQUIRE(whole);
    REQUIRE(whole->x <= bounds->x);
}

TEST_CASE("Readback pixels become straight RGBA and swap BGRA channels", "[export]") {
    std::uint8_t rgba[8] = {64, 32, 16, 128, 10, 20, 30, 0};
    vc::unpremultiplyPixels(rgba, 2, false);
    REQUIRE(rgba[0] == 128);
    REQUIRE(rgba[1] == 64);
    REQUIRE(rgba[2] == 32);
    REQUIRE(rgba[3] == 128);
    REQUIRE(rgba[4] == 10);

    std::uint8_t bgra[4] = {30, 20, 10, 255};
    vc::unpremultiplyPixels(bgra, 1, true);
    REQUIRE(bgra[0] == 10);
    REQUIRE(bgra[1] == 20);
    REQUIRE(bgra[2] == 30);
}
