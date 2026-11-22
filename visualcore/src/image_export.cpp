#include "vc/image_export.hpp"

#include <algorithm>
#include <cmath>
#include "vc/geometry.hpp"

namespace vc {
namespace {

Rect intersect(const Rect& a, const Rect& b) {
    const double x0 = std::max(a.x, b.x), y0 = std::max(a.y, b.y);
    const double x1 = std::min(a.x + a.w, b.x + b.w), y1 = std::min(a.y + a.h, b.y + b.h);
    return {x0, y0, std::max(0.0, x1 - x0), std::max(0.0, y1 - y0)};
}

bool within(const Document& document, const Id& ancestor, Id id) {
    for (int depth = 0; depth < 4096 && !id.isNil(); ++depth) {
        if (id == ancestor) return true;
        const Node* node = document.view(id);
        if (!node) return false;
        id = node->parent;
    }
    return false;
}

} // namespace

std::optional<ExportPlan> planExport(const Rect& bounds, double scale, std::uint32_t maxTexture) {
    if (!(bounds.w > 0.0) || !(bounds.h > 0.0) || !(scale > 0.0) || !std::isfinite(scale) || maxTexture == 0) return std::nullopt;
    ExportPlan plan;
    plan.requestedScale = scale;
    const double limit = std::min({double(kMaxExportSide) / bounds.w, double(kMaxExportSide) / bounds.h,
                                   std::sqrt(double(kMaxExportPixels) / (bounds.w * bounds.h))}) * (1.0 - 1e-9);
    plan.scale = std::min(scale, limit);
    plan.clamped = plan.scale < scale;
    plan.width = std::max<std::uint32_t>(1, std::uint32_t(std::ceil(bounds.w * plan.scale - 1e-9)));
    plan.height = std::max<std::uint32_t>(1, std::uint32_t(std::ceil(bounds.h * plan.scale - 1e-9)));
    plan.tileSize = std::min(maxTexture, kMaxExportTile);
    plan.columns = (plan.width + plan.tileSize - 1) / plan.tileSize;
    plan.rows = (plan.height + plan.tileSize - 1) / plan.tileSize;
    return plan;
}

TilePixels exportTilePixels(const ExportPlan& plan, std::uint32_t column, std::uint32_t row) {
    TilePixels tile;
    tile.x = column * plan.tileSize;
    tile.y = row * plan.tileSize;
    tile.w = std::min(plan.tileSize, plan.width - tile.x);
    tile.h = std::min(plan.tileSize, plan.height - tile.y);
    return tile;
}

Rect exportTileWorld(const ExportPlan& plan, const Rect& bounds, std::uint32_t column, std::uint32_t row) {
    const TilePixels tile = exportTilePixels(plan, column, row);
    return {bounds.x + tile.x / plan.scale, bounds.y + tile.y / plan.scale, tile.w / plan.scale, tile.h / plan.scale};
}

std::optional<Rect> buildExportDrawList(const Document& document, const Id& id, const Rect& view, const Id& mode,
                                        std::vector<DrawItem>& out) {
    const Node* node = document.view(id);
    const auto bounds = worldBounds(document, id, mode);
    if (!node || !node->visible || !bounds) return std::nullopt;
    std::vector<DrawItem> all;
    buildDrawList(document, intersect(view, *bounds), mode, all);
    out.clear();
    for (DrawItem& item : all) {
        if (!within(document, id, item.node)) continue;
        item.clip = intersect(item.clip, *bounds);
        out.push_back(std::move(item));
    }
    return bounds;
}

void unpremultiplyPixels(std::uint8_t* pixels, std::size_t pixelCount, bool bgra) {
    for (std::size_t i = 0; i < pixelCount; ++i) {
        std::uint8_t* p = pixels + i * 4;
        if (bgra) std::swap(p[0], p[2]);
        const unsigned a = p[3];
        if (a == 0 || a == 255) continue;
        for (int c = 0; c < 3; ++c) p[c] = std::uint8_t(std::min(255u, (p[c] * 255u + a / 2) / a));
    }
}

} // namespace vc
