#include "vc/tokens.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace vc {
namespace {

constexpr std::array<std::pair<TokenType, std::string_view>, 9> kTypeNames{{
    {TokenType::Color, "color"},
    {TokenType::Dimension, "dimension"},
    {TokenType::Duration, "duration"},
    {TokenType::Ratio, "ratio"},
    {TokenType::Opacity, "opacity"},
    {TokenType::Border, "border"},
    {TokenType::Shadow, "shadow"},
    {TokenType::Typography, "typography"},
    {TokenType::Gradient, "gradient"},
}};

constexpr std::array<std::pair<Unit, std::string_view>, 7> kUnitNames{{
    {Unit::None, ""},
    {Unit::Px, "px"},
    {Unit::Rem, "rem"},
    {Unit::Em, "em"},
    {Unit::Pt, "pt"},
    {Unit::Ms, "ms"},
    {Unit::S, "s"},
}};

bool isLower(char c) {
    return c >= 'a' && c <= 'z';
}

bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

} // namespace

bool isValidIdentifier(std::string_view identifier) {
    // ^[a-z][a-z0-9]*(-[a-z0-9]+)*(\.[a-z][a-z0-9]*(-[a-z0-9]+)*)*$
    std::size_t i = 0;
    while (true) {
        if (i >= identifier.size() || !isLower(identifier[i])) {
            return false;
        }
        ++i;
        while (i < identifier.size() && (isLower(identifier[i]) || isDigit(identifier[i]))) {
            ++i;
        }
        while (i < identifier.size() && identifier[i] == '-') {
            ++i;
            const std::size_t start = i;
            while (i < identifier.size() && (isLower(identifier[i]) || isDigit(identifier[i]))) {
                ++i;
            }
            if (i == start) {
                return false;
            }
        }
        if (i == identifier.size()) {
            return true;
        }
        if (identifier[i] != '.') {
            return false;
        }
        ++i;
    }
}

std::string_view tokenTypeName(TokenType type) {
    for (const auto& [value, name] : kTypeNames) {
        if (value == type) return name;
    }
    return "color";
}

std::optional<TokenType> tokenTypeFromName(std::string_view name) {
    for (const auto& [value, text] : kTypeNames) {
        if (text == name) return value;
    }
    return std::nullopt;
}

std::string_view unitName(Unit unit) {
    for (const auto& [value, name] : kUnitNames) {
        if (value == unit) return name;
    }
    return "";
}

std::optional<Unit> unitFromName(std::string_view name) {
    for (const auto& [value, text] : kUnitNames) {
        if (text == name) return value;
    }
    return std::nullopt;
}

std::vector<Unit> unitsFor(TokenType type) {
    switch (type) {
    case TokenType::Dimension:
        return {Unit::Px, Unit::Rem, Unit::Em, Unit::Pt};
    case TokenType::Duration:
        return {Unit::Ms, Unit::S};
    case TokenType::Ratio:
    case TokenType::Opacity:
        return {Unit::None};
    case TokenType::Color:
    case TokenType::Border:
    case TokenType::Shadow:
    case TokenType::Gradient:
    case TokenType::Typography:
        break;
    }
    return {};
}

const char* gradientGeometryProblem(const GradientValue& g) {
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

std::vector<double> completeStopPositions(const std::vector<GradientStopValue>& stops) {
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

void applyCssStopOrder(std::vector<double>& positions) {
    for (std::size_t i = 1; i < positions.size(); ++i) positions[i] = std::max(positions[i], positions[i - 1]);
}

void normalizeGradientGeometry(GradientValue& g) {
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

} // namespace vc
