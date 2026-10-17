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

} // namespace vc
