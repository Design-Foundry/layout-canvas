#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "vc/font.hpp"
#include "vc/text.hpp"
struct hb_face_t;
#include "vc/math.hpp"

namespace vc::fonts {

inline constexpr double kAtlasEm = 96.0;
inline constexpr std::string_view kSerifFace = "embedded/source-serif-4";
inline constexpr std::string_view kMonoFace = "embedded/jetbrains-mono";

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
    std::uint32_t instance = 0;
};

[[nodiscard]] std::uint32_t tag(std::string_view name);
/// Index of `face` at `variations`, created on first use; -1 when no face has that id.
[[nodiscard]] int instanceIndex(std::string_view face, std::span<const Variation> variations);
[[nodiscard]] Metrics metrics(std::uint32_t instance);
/// True for characters that attach to the one before them: combining marks, joiners and variation
/// selectors.
[[nodiscard]] bool attachesToPrevious(std::uint32_t codepoint);
/// The instance's missing-glyph mark, with its advance and ink.
[[nodiscard]] ShapedGlyph missingGlyph(std::uint32_t instance);
/// True if the instance's font maps `codepoint` to a glyph.
[[nodiscard]] bool covers(std::uint32_t instance, std::uint32_t codepoint);
/// Shapes one line of text with no newlines, with `features` on top of the font's defaults.
[[nodiscard]] std::vector<ShapedGlyph> shape(std::uint32_t instance, std::string_view text, const FontFeatures& features = {});

struct ReadAxis;
struct Capabilities;
[[nodiscard]] Capabilities capabilitiesOf(hb_face_t* face);

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
/// The files declared for a family and version, or nothing.
[[nodiscard]] std::optional<std::vector<FontFile>> familyFiles(std::string_view provider, std::string_view key, std::string_view version);

} // namespace vc::fonts
