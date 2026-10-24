#pragma once
#include <array>
#include <cstddef>
#include <vector>
#include "vc/color.hpp"
#include "vc/document.hpp"
#include "vc/math.hpp"

namespace vc {

enum class DrawKind { Fill, Stroke, DropShadow, InnerShadow, Outline, Glyph, Segment, Path, Image };

/// A rectangle with a radius per corner: top-start, top-end, bottom-end, bottom-start.
struct RoundBox {
    Rect rect;
    std::array<double, 4> radii{};
};

/// One primitive for the renderer, in world space and paint order.
///  - Fill: `a`.
///  - Stroke: inside `a`, outside `b`.
///  - DropShadow: `a` blurred by `sigma`.
///  - InnerShadow: inside `b`, outside `a` blurred by `sigma`.
///  - Outline: a thin ring along `a`, for selection.
///  - Glyph: `quad` with normalized atlas coordinates in `a.rect`.
///  - Segment: round-cap line between the zero-size `a.rect`/`b.rect` origins,
///    with diameter `strokeWidth`. `quad` includes both cap extents.
struct DrawItem {
    DrawKind kind = DrawKind::Fill;
    bool ellipse = false;
    /// The area the item may touch. The renderer adds antialiasing margin.
    Rect quad;
    RoundBox a;
    RoundBox b;
    Color color;
    /// World-space clip from frames that clip their content.
    Rect clip;
    double sigma = 0.0;
    StrokeStyle style = StrokeStyle::Solid;
    double strokeWidth = 0.0;
    /// One union-distance primitive, avoiding alpha overlap at round joins.
    std::vector<Vec2> points{};
    bool closed = false;
    bool filled = false;
    const Raster* image = nullptr;
};

/// Fills `out` with what's visible inside `view`, in paint order, resolving tokens in `mode`.
/// Returns how many nodes contributed at least one item.
std::size_t buildDrawList(const Document& document, const Rect& view, const Id& mode, std::vector<DrawItem>& out);

/// The color drawn for a token reference that can't be resolved.
inline constexpr Color kUnresolvedColor{1.0f, 0.0f, 1.0f, 1.0f};

} // namespace vc
