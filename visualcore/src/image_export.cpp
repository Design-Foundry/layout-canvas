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

std::optional<Rect> exportBounds(const Document& document, const Id& id, const Id& mode) {
    const Node* node = document.view(id);
    const auto box = worldBounds(document, id, mode);
    if (!node || !node->visible || !box) return std::nullopt;
    std::vector<DrawItem> items;
    buildIsolatedDrawList(document, id, {-1e9, -1e9, 2e9, 2e9}, mode, items);
    double x0 = box->x, y0 = box->y, x1 = box->x + box->w, y1 = box->y + box->h;
    for (const DrawItem& item : items) {
        const Rect r = intersect(item.quad, item.clip);
        if (!(r.w > 0.0) || !(r.h > 0.0)) continue;
        x0 = std::min(x0, r.x);
        y0 = std::min(y0, r.y);
        x1 = std::max(x1, r.x + r.w);
        y1 = std::max(y1, r.y + r.h);
    }
    if (x0 < box->x) x0 = std::floor(x0);
    if (y0 < box->y) y0 = std::floor(y0);
    if (x1 > box->x + box->w) x1 = std::ceil(x1);
    if (y1 > box->y + box->h) y1 = std::ceil(y1);
    return Rect{x0, y0, x1 - x0, y1 - y0};
}

bool buildExportDrawList(const Document& document, const Id& id, const Rect& view, const Id& mode,
                         std::vector<DrawItem>& out, double pixelScale) {
    return buildIsolatedDrawList(document, id, view, mode, out, pixelScale);
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
