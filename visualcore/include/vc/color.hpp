#pragma once

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

} // namespace vc
