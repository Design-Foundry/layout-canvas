#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "vc/font.hpp"
#include "vc/math.hpp"

namespace vc::fonts {

inline constexpr double kAtlasEm = 96.0;

struct Variation {
    std::uint32_t tag = 0;
    float value = 0;
};

/// Vertical metrics of an instance in atlas pixels.
struct Metrics {
    double ascent = 0;
    double descent = 0;
};

/// One shaped glyph. Lengths are atlas pixels; `ink` is the glyph's pixel box at the atlas size.
struct ShapedGlyph {
    std::uint32_t glyph = 0;
    /// Byte offset of the glyph's first character in the shaped text.
    std::uint32_t cluster = 0;
    double advance = 0;
    /// Adjustment toward the next glyph, such as kerning.
    double kern = 0;
    double offsetX = 0;
    double offsetY = 0;
    Rect ink;
};

[[nodiscard]] std::uint32_t tag(std::string_view name);
/// Index of `face` at `variations`, created on first use; -1 when no face has that id.
[[nodiscard]] int instanceIndex(std::string_view face, std::span<const Variation> variations);
[[nodiscard]] Metrics metrics(std::uint32_t instance);
/// Shapes one line of text with no newlines, with `features` on top of the font's defaults.
[[nodiscard]] std::vector<ShapedGlyph> shape(std::uint32_t instance, std::string_view text, const FontFeatures& features = {});

struct ReadAxis {
    std::string tag;
    double min = 0, max = 0;
    double defaultValue = 0;
};
/// What a registered face offers, read from its bytes.
struct Capabilities {
    std::vector<ReadAxis> axes;
    /// GSUB and GPOS feature tags, sorted and unique.
    std::vector<std::string> features;
};
/// Nothing when no face has that id.
[[nodiscard]] std::optional<Capabilities> capabilities(std::string_view face);

} // namespace vc::fonts
