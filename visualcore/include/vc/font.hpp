#pragma once
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include "vc/id.hpp"

namespace vc {

/// Where a font stack ends when none of its fonts covers a character.
enum class GenericFallback { SansSerif, Serif, Monospace, SystemUi, None };

/// The values an axis accepts: the interval [min, max] when `values` is empty, otherwise exactly those
/// points in ascending order.
struct AxisDomain {
    double min = 0;
    double max = 0;
    std::vector<double> values;
    [[nodiscard]] bool allows(double value) const;
    friend bool operator==(const AxisDomain&, const AxisDomain&) = default;
};

struct FontAxis {
    std::string tag;
    AxisDomain domain;
    std::optional<double> defaultValue;
    friend bool operator==(const FontAxis&, const FontAxis&) = default;
};

/// A font the document may use: who provides its bytes and what it allows. The shape of a Design
/// Foundry font resource.
struct Font {
    Id id;
    std::string identifier;
    std::string displayName;
    std::string provider;
    std::string key;
    /// SHA-256 of the face bytes in lowercase hex, empty when the provider does not pin it.
    std::string sha256;
    std::vector<FontAxis> axes;
    std::vector<std::string> features;
    std::string order;
    [[nodiscard]] const FontAxis* axis(std::string_view tag) const;
    /// The registered face that holds this font's bytes.
    [[nodiscard]] std::string face() const { return provider + "/" + key; }
    friend bool operator==(const Font&, const Font&) = default;
};

/// Ordered fonts plus the generic family that ends them. Text validates against the first font.
struct FontStack {
    Id id;
    std::string identifier;
    std::string displayName;
    std::vector<Id> fonts;
    GenericFallback fallback = GenericFallback::SansSerif;
    std::string order;
    friend bool operator==(const FontStack&, const FontStack&) = default;
};

using FontAxes = std::map<std::string, double>;
/// An OpenType feature setting: on, off, or an alternate's index.
using FontFeatureValue = std::variant<bool, int>;
using FontFeatures = std::map<std::string, FontFeatureValue>;

/// The bundled Inter and its one-font stack. Every document has both and cannot change them.
inline constexpr Id kInterFontId{0, 0x100};
inline constexpr Id kInterStackId{0, 0x101};
[[nodiscard]] const Font& embeddedInterFont();
[[nodiscard]] const FontStack& embeddedInterStack();

/// Four printable ASCII characters, as OpenType tags are.
[[nodiscard]] bool isValidFontTag(std::string_view tag);
[[nodiscard]] std::string_view genericFallbackName(GenericFallback fallback);
[[nodiscard]] std::optional<GenericFallback> genericFallbackFromName(std::string_view name);
/// Why `font` is malformed on its own, or null.
[[nodiscard]] const char* fontProblem(const Font& font);
/// Why `axes` and `features` are malformed on their own, or null.
[[nodiscard]] const char* fontSettingsProblem(const FontAxes& axes, const FontFeatures& features);
/// Why `font` refuses `axes` or `features`, or null.
[[nodiscard]] std::optional<std::string> fontRefuses(const Font& font, const FontAxes& axes, const FontFeatures& features);
/// The axis values a retired `inter-*-v1` font id stood for, or nothing for other strings.
[[nodiscard]] std::optional<FontAxes> legacyInterAxes(std::string_view font);

} // namespace vc
