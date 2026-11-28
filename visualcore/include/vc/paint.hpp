#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <variant>
#include <vector>
#include "vc/color.hpp"
#include "vc/value.hpp"

namespace vc {

enum class GradientKind { Linear, Radial, Conic };
enum class RadialShape { Circle, Ellipse };
enum class RadialExtent { ClosestSide, ClosestCorner, FarthestSide, FarthestCorner };

/// Why gradient geometry (`geometry`, `angle`, `shape`, `extent`, `centerX`, `centerY`) is refused, or
/// null. Fields that don't apply to the geometry are refused, not ignored; a center needs both
/// coordinates. Works on any type that carries those fields.
template <typename G>
[[nodiscard]] const char* gradientGeometryProblem(const G& g) {
    if (g.angle && !std::isfinite(*g.angle)) return "a gradient angle must be a finite number";
    if (g.centerX.has_value() != g.centerY.has_value()) return "a gradient center needs both x and y";
    if (g.centerX && (!std::isfinite(*g.centerX) || !std::isfinite(*g.centerY))) return "a gradient center must be finite numbers";
    switch (g.geometry) {
    case GradientKind::Linear:
        if (g.shape || g.extent || g.centerX) return "a linear gradient takes an angle only; shape, extent and center do not apply";
        break;
    case GradientKind::Radial:
        if (g.angle) return "a radial gradient has no angle";
        break;
    case GradientKind::Conic:
        if (g.shape || g.extent) return "a conic gradient takes a from angle and a center; shape and extent do not apply";
        break;
    }
    return nullptr;
}

/// Fills the defaults for the geometry: angle 0 (linear, conic), ellipse and farthest-corner, center 50/50.
template <typename G>
void normalizeGradientGeometry(G& g) {
    if (g.geometry != GradientKind::Radial && !g.angle) g.angle = 0.0;
    if (g.geometry == GradientKind::Radial) {
        if (!g.shape) g.shape = RadialShape::Ellipse;
        if (!g.extent) g.extent = RadialExtent::FarthestCorner;
    }
    if (g.geometry != GradientKind::Linear && !g.centerX) {
        g.centerX = 50.0;
        g.centerY = 50.0;
    }
}

/// Stop positions with the omitted ones completed (first 0, last 1, the rest spread evenly between
/// known neighbors). Positions stay as authored otherwise. Works on any stop type with an optional
/// `position`.
template <typename Stops>
[[nodiscard]] std::vector<double> completeStopPositions(const Stops& stops) {
    std::vector<double> out(stops.size(), 0.0);
    std::vector<bool> known(stops.size(), false);
    for (std::size_t i = 0; i < stops.size(); ++i) {
        if (stops[i].position) {
            out[i] = *stops[i].position;
            known[i] = true;
        }
    }
    if (stops.empty()) return out;
    if (!known.front()) { out.front() = 0.0; known.front() = true; }
    if (!known.back()) { out.back() = 1.0; known.back() = true; }
    std::size_t start = 0;
    for (std::size_t i = 1; i < stops.size(); ++i) {
        if (!known[i]) continue;
        for (std::size_t k = start + 1; k < i; ++k) {
            out[k] = out[start] + (out[i] - out[start]) * double(k - start) / double(i - start);
        }
        start = i;
    }
    return out;
}

/// CSS stop order: a position lower than the one before it is raised to it. Applied when drawing.
inline void applyCssStopOrder(std::vector<double>& positions) {
    for (std::size_t i = 1; i < positions.size(); ++i) positions[i] = std::max(positions[i], positions[i - 1]);
}

/// One stop of a gradient paint: a color (literal or a color token) at an optional position along the
/// gradient line, 0 to 1. Omitted positions are completed when the gradient is resolved.
struct GradientStop {
    Value<Oklch> color = Oklch{};
    std::optional<double> position;

    friend bool operator==(const GradientStop&, const GradientStop&) = default;
};

/// A gradient written on a node, with the same geometry as a gradient token.
struct Gradient {
    GradientKind geometry = GradientKind::Linear;
    bool repeating = false;
    std::optional<double> angle;
    std::optional<RadialShape> shape;
    std::optional<RadialExtent> extent;
    std::optional<double> centerX;
    std::optional<double> centerY;
    std::vector<GradientStop> stops;

    friend bool operator==(const Gradient&, const Gradient&) = default;
};

/// What a paint draws: a solid color, a gradient, or a token that is a color or a gradient.
using PaintSource = std::variant<Oklch, TokenRef, Gradient>;

/// One layer of a node's fill stack. Images will join `PaintSource` later.
struct Paint {
    PaintSource source = Oklch{};
    bool visible = true;
    /// 0 to 1, multiplies the paint's own alpha.
    double opacity = 1.0;

    Paint() = default;
    Paint(PaintSource s) : source(std::move(s)) {}
    Paint(Oklch c) : source(c) {}
    Paint(TokenRef t) : source(t) {}
    Paint(Gradient g) : source(std::move(g)) {}

    friend bool operator==(const Paint&, const Paint&) = default;
    /// A visible, fully opaque solid color or token: the paint legacy documents write as `fill`.
    [[nodiscard]] bool isPlain() const {
        return visible && opacity == 1.0 && !std::holds_alternative<Gradient>(source);
    }
};

inline constexpr std::size_t kMaxFills = 16;
inline constexpr std::size_t kMaxGradientStops = 32;

} // namespace vc
