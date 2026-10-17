#pragma once
#include <optional>
#include <string>
#include <string_view>

namespace vc {

/// Gamma-encoded sRGB, channels in [0, 1]. Only the renderer deals in it.
struct Color {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

/// Document color. Lightness in [0, 1], chroma >= 0, hue in degrees.
struct Oklch {
    double l = 0.0;
    double c = 0.0;
    double h = 0.0;
    double a = 1.0;

    friend bool operator==(const Oklch&, const Oklch&) = default;
};

/// Converts to sRGB, clipping colors outside the gamut.
[[nodiscard]] Color toSrgb(const Oklch& color);

/// Converts gamma-encoded sRGB channels in [0, 1]. Grays get hue 0.
[[nodiscard]] Oklch fromSrgb(double r, double g, double b, double a = 1.0);

/// `#rrggbb`, ignoring alpha.
[[nodiscard]] std::string toHex(const Oklch& color);

/// Parses `#rgb` or `#rrggbb` (the `#` is optional) into a color with alpha `a`.
[[nodiscard]] std::optional<Oklch> fromHex(std::string_view hex, double a = 1.0);

} // namespace vc
