#include "vc/color.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace vc {
namespace {

float encode(double linear) {
    const double v = linear <= 0.0031308 ? 12.92 * linear : 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
    return static_cast<float>(std::clamp(v, 0.0, 1.0));
}

double decode(double encoded) {
    return encoded <= 0.04045 ? encoded / 12.92 : std::pow((encoded + 0.055) / 1.055, 2.4);
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

constexpr double kAchromaticChroma = 1e-4;

} // namespace

Color toSrgb(const Oklch& color) {
    const double hue = color.h * std::numbers::pi / 180.0;
    const double a = color.c * std::cos(hue);
    const double b = color.c * std::sin(hue);

    const double l = std::pow(color.l + 0.3963377774 * a + 0.2158037573 * b, 3.0);
    const double m = std::pow(color.l - 0.1055613458 * a - 0.0638541728 * b, 3.0);
    const double s = std::pow(color.l - 0.0894841775 * a - 1.2914855480 * b, 3.0);

    return {
        encode(4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s),
        encode(-1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s),
        encode(-0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s),
        static_cast<float>(std::clamp(color.a, 0.0, 1.0)),
    };
}

Oklch fromSrgb(double r, double g, double b, double a) {
    const double lr = decode(r);
    const double lg = decode(g);
    const double lb = decode(b);

    const double l = std::cbrt(0.4122214708 * lr + 0.5363325363 * lg + 0.0514459929 * lb);
    const double m = std::cbrt(0.2119034982 * lr + 0.6806995451 * lg + 0.1073969566 * lb);
    const double s = std::cbrt(0.0883024619 * lr + 0.2817188376 * lg + 0.6299787005 * lb);

    const double lightness = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s;
    const double labA = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
    const double labB = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;

    const double chroma = std::hypot(labA, labB);
    if (chroma < kAchromaticChroma) {
        return {lightness, 0.0, 0.0, a};
    }
    double hue = std::atan2(labB, labA) * 180.0 / std::numbers::pi;
    if (hue < 0.0) {
        hue += 360.0;
    }
    return {lightness, chroma, hue, a};
}

std::string toHex(const Oklch& color) {
    constexpr char kDigits[] = "0123456789abcdef";
    const Color srgb = toSrgb(color);
    std::string out = "#";
    for (float channel : {srgb.r, srgb.g, srgb.b}) {
        const int value = static_cast<int>(std::lround(channel * 255.0f));
        out.push_back(kDigits[value >> 4]);
        out.push_back(kDigits[value & 0xF]);
    }
    return out;
}

std::optional<Oklch> fromHex(std::string_view hex, double a) {
    if (!hex.empty() && hex.front() == '#') {
        hex.remove_prefix(1);
    }
    if (hex.size() != 3 && hex.size() != 6) {
        return std::nullopt;
    }
    const std::size_t width = hex.size() / 3;
    double channels[3];
    for (std::size_t i = 0; i < 3; ++i) {
        int value = 0;
        for (std::size_t j = 0; j < width; ++j) {
            const int digit = hexDigit(hex[i * width + j]);
            if (digit < 0) {
                return std::nullopt;
            }
            value = value * 16 + digit;
        }
        channels[i] = width == 1 ? value * 17 / 255.0 : value / 255.0;
    }
    return fromSrgb(channels[0], channels[1], channels[2], a);
}

} // namespace vc
