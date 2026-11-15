#include "vc/font.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include "fonts.hpp"
#include "vc/text.hpp"
#include "vc/tokens.hpp"

namespace vc {
namespace {
constexpr double kEpsilon = 1e-4;

bool lowercaseHex(std::string_view value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
} // namespace

bool AxisDomain::allows(double value) const {
    if (!std::isfinite(value)) return false;
    if (values.empty()) return value >= min - kEpsilon && value <= max + kEpsilon;
    return std::any_of(values.begin(), values.end(), [&](double v) { return std::abs(v - value) < kEpsilon; });
}

const FontAxis* Font::axis(std::string_view tag) const {
    const auto it = std::find_if(axes.begin(), axes.end(), [&](const FontAxis& a) { return a.tag == tag; });
    return it == axes.end() ? nullptr : &*it;
}

const Font& embeddedInterFont() {
    static const Font font = [] {
        Font out;
        out.id = kInterFontId;
        out.identifier = "inter";
        out.displayName = "Inter";
        out.provider = "embedded";
        out.key = "inter";
        out.sha256 = fontFaceSha256(kEmbeddedInterFace);
        const auto read = *fonts::capabilities(kEmbeddedInterFace);
        for (const auto& axis : read.axes) out.axes.push_back({axis.tag, {axis.min, axis.max, {}}, axis.defaultValue});
        out.features = read.features;
        out.order = "a0";
        return out;
    }();
    return font;
}

const FontStack& embeddedInterStack() {
    static const FontStack stack{kInterStackId, "inter", "Inter", {kInterFontId}, GenericFallback::SansSerif, "a0"};
    return stack;
}

bool isValidFontTag(std::string_view tag) {
    return tag.size() == 4 && std::all_of(tag.begin(), tag.end(), [](char c) { return c >= 0x20 && c <= 0x7e; });
}

std::string_view genericFallbackName(GenericFallback fallback) {
    switch (fallback) {
    case GenericFallback::Serif: return "serif";
    case GenericFallback::Monospace: return "monospace";
    case GenericFallback::SystemUi: return "system-ui";
    case GenericFallback::None: return "none";
    default: return "sans-serif";
    }
}

std::optional<GenericFallback> genericFallbackFromName(std::string_view name) {
    for (auto fallback : {GenericFallback::SansSerif, GenericFallback::Serif, GenericFallback::Monospace, GenericFallback::SystemUi, GenericFallback::None})
        if (genericFallbackName(fallback) == name) return fallback;
    return std::nullopt;
}

const char* fontProblem(const Font& font) {
    if (font.id.isNil()) return "a font needs an id";
    if (!isValidIdentifier(font.identifier)) return "identifier must be lowercase words separated by - or .";
    if (font.provider.empty() || font.provider.size() > 64 || font.provider.find('/') != std::string::npos)
        return "provider must have 1 to 64 characters and no /";
    if (font.key.empty() || font.key.size() > 256) return "key must have 1 to 256 characters";
    if (font.displayName.size() > 256) return "displayName must have at most 256 characters";
    if (!font.sha256.empty() && !lowercaseHex(font.sha256)) return "sha256 must be 64 lowercase hexadecimal digits";
    if (font.axes.size() > 64) return "a font has at most 64 axes";
    std::set<std::string> tags;
    for (const auto& axis : font.axes) {
        if (!isValidFontTag(axis.tag)) return "axis tags must be four printable ASCII characters";
        if (!tags.insert(axis.tag).second) return "axis tags must be unique";
        const auto& d = axis.domain;
        if (d.values.empty()) {
            if (!std::isfinite(d.min) || !std::isfinite(d.max) || d.min > d.max) return "an axis interval needs finite min <= max";
        } else {
            if (d.min != 0 || d.max != 0) return "an axis domain has either min and max or values";
            if (d.values.size() > 1000) return "an axis lists at most 1000 values";
            for (std::size_t i = 0; i < d.values.size(); ++i)
                if (!std::isfinite(d.values[i]) || (i && d.values[i] <= d.values[i - 1])) return "axis values must be finite and ascending";
        }
        if (axis.defaultValue && !d.allows(*axis.defaultValue)) return "an axis default must be one of its values";
    }
    if (font.features.size() > 512) return "a font declares at most 512 features";
    std::set<std::string> features;
    for (const auto& feature : font.features) {
        if (!isValidFontTag(feature)) return "feature tags must be four printable ASCII characters";
        if (!features.insert(feature).second) return "feature tags must be unique";
    }
    return nullptr;
}

const char* fontSettingsProblem(const FontAxes& axes, const FontFeatures& features) {
    if (axes.size() > 64) return "axes has at most 64 entries";
    for (const auto& [tag, value] : axes) {
        if (!isValidFontTag(tag)) return "axis tags must be four printable ASCII characters";
        if (!std::isfinite(value)) return "axis values must be finite";
    }
    if (features.size() > 512) return "features has at most 512 entries";
    for (const auto& [tag, value] : features) {
        if (!isValidFontTag(tag)) return "feature tags must be four printable ASCII characters";
        if (const auto* index = std::get_if<int>(&value); index && (*index < 0 || *index > 65535)) return "a feature value must be true, false or 0 to 65535";
    }
    return nullptr;
}

std::optional<std::string> fontRefuses(const Font& font, const FontAxes& axes, const FontFeatures& features) {
    for (const auto& [tag, value] : axes) {
        const auto* axis = font.axis(tag);
        if (!axis) return "font \"" + font.identifier + "\" has no " + tag + " axis";
        if (!axis->domain.allows(value)) return "font \"" + font.identifier + "\" does not allow " + tag + " " + std::to_string(value);
    }
    for (const auto& [tag, value] : features)
        if (std::find(font.features.begin(), font.features.end(), tag) == font.features.end())
            return "font \"" + font.identifier + "\" does not declare the " + tag + " feature";
    return std::nullopt;
}

std::optional<FontAxes> legacyInterAxes(std::string_view font) {
    if (font == "inter-regular-v1") return FontAxes{};
    if (font == "inter-bold-v1") return FontAxes{{"wght", 700}};
    if (font == "inter-oblique-v1") return FontAxes{{"slnt", -10}};
    if (font == "inter-bold-oblique-v1") return FontAxes{{"slnt", -10}, {"wght", 700}};
    return std::nullopt;
}

} // namespace vc
