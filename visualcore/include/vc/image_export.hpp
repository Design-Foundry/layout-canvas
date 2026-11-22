#pragma once
#include <cstdint>
#include <optional>
#include <vector>
#include "vc/draw_list.hpp"

namespace vc {

inline constexpr std::uint32_t kMaxExportSide = 16384;
inline constexpr std::uint64_t kMaxExportPixels = 64ull * 1024 * 1024;
inline constexpr std::uint32_t kMaxExportTile = 4096;

/// How an export of a node's bounds is laid out in pixels.
struct ExportPlan {
    double requestedScale = 1.0;
    /// The scale actually used: lower than requested only when the image would exceed the size limits.
    double scale = 1.0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t tileSize = 0;
    std::uint32_t columns = 1;
    std::uint32_t rows = 1;
    bool clamped = false;
    [[nodiscard]] bool tiled() const { return columns * rows > 1; }
};

/// Plans an export of `bounds` at `scale`. The scale is clamped so neither side exceeds kMaxExportSide
/// and the image stays within kMaxExportPixels. Targets larger than `maxTexture` or kMaxExportTile are
/// split into square tiles. Empty without a usable size or scale.
[[nodiscard]] std::optional<ExportPlan> planExport(const Rect& bounds, double scale, std::uint32_t maxTexture);

/// World-space rectangle of tile (`column`, `row`) in a plan whose image starts at `bounds`.
[[nodiscard]] Rect exportTileWorld(const ExportPlan& plan, const Rect& bounds, std::uint32_t column, std::uint32_t row);

/// Pixel rectangle of a tile within the full image.
struct TilePixels {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t w = 0;
    std::uint32_t h = 0;
};
[[nodiscard]] TilePixels exportTilePixels(const ExportPlan& plan, std::uint32_t column, std::uint32_t row);

/// The rectangle an export of node `id` covers: its box grown to everything it paints, such as drop
/// shadows, outside or center strokes and children that overflow it. Content clipped by a descendant
/// frame counts only where it shows. Grown edges are rounded outward to whole units. Nothing if the node
/// does not exist or is hidden.
[[nodiscard]] std::optional<Rect> exportBounds(const Document& document, const Id& id, const Id& mode);

/// Fills `out` with the paint of node `id` and its descendants that falls inside `view`, in paint order,
/// without the clipping, opacity or paint of its ancestors. Canvas overlays are never part of it. False
/// if the node does not exist or is hidden.
bool buildExportDrawList(const Document& document, const Id& id, const Rect& view, const Id& mode,
                         std::vector<DrawItem>& out, double pixelScale = 1.0);

/// Converts premultiplied RGBA (or BGRA when `bgra`) to straight RGBA in place.
void unpremultiplyPixels(std::uint8_t* pixels, std::size_t pixelCount, bool bgra);

} // namespace vc
