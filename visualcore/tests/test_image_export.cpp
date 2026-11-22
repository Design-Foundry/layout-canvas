#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "vc/image_export.hpp"

using Catch::Matchers::WithinAbs;
using vc::CreateNode;
using vc::Document;
using vc::Id;
using vc::Node;
using vc::NodeType;
using vc::Rect;

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

TEST_CASE("The export draw list holds only the node subtree, clipped to its bounds", "[export]") {
    Document doc;
    auto target = frame(1, 100, 100, 200, 100);
    REQUIRE(doc.apply(CreateNode{target}));
    REQUIRE(doc.apply(CreateNode{frame(2, 50, 20, 100, 40, 1)}));
    REQUIRE(doc.apply(CreateNode{frame(3, 90, 90, 300, 300)}));
    REQUIRE(doc.apply(CreateNode{frame(4, 500, 500, 10, 10)}));

    std::vector<vc::DrawItem> items;
    const Rect view{-1000, -1000, 4000, 4000};
    const auto bounds = vc::buildExportDrawList(doc, target.id, view, vc::kDefaultModeId, items);
    REQUIRE(bounds);
    REQUIRE(bounds->x == 100);
    REQUIRE(bounds->w == 200);
    REQUIRE_FALSE(items.empty());
    for (const auto& item : items) {
        REQUIRE((item.node == target.id || item.node == Id{4, 2}));
        REQUIRE(item.clip.x >= bounds->x);
        REQUIRE(item.clip.x + item.clip.w <= bounds->x + bounds->w);
        REQUIRE(item.clip.y + item.clip.h <= bounds->y + bounds->h);
    }
}

TEST_CASE("A nested node exports without its siblings or the frame behind it", "[export]") {
    Document doc;
    REQUIRE(doc.apply(CreateNode{frame(1, 0, 0, 400, 400)}));
    REQUIRE(doc.apply(CreateNode{frame(2, 10, 10, 100, 100, 1)}));
    REQUIRE(doc.apply(CreateNode{frame(3, 50, 50, 100, 100, 1)}));
    std::vector<vc::DrawItem> items;
    const auto bounds = vc::buildExportDrawList(doc, Id{4, 2}, {0, 0, 400, 400}, vc::kDefaultModeId, items);
    REQUIRE(bounds);
    REQUIRE_FALSE(items.empty());
    for (const auto& item : items) REQUIRE(item.node == Id{4, 2});
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
