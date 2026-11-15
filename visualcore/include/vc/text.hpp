#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <optional>
#include <string_view>
#include <vector>
#include "vc/math.hpp"

namespace vc {

enum class TextAlign { Left, Center, Right };

/// A single face and style for an editable text node.
struct TextRun {
    std::string content;
    std::string font = "inter-regular-v1";
    double fontSize = 16.0;
    double lineHeight = 1.4;
    double letterSpacing = 0.0;
    TextAlign align = TextAlign::Left;
    friend bool operator==(const TextRun&, const TextRun&) = default;
};

struct TextStyle {
    std::optional<double> fontSize;
    std::optional<double> lineHeight;
    std::optional<double> letterSpacing;
    std::optional<TextAlign> align;
    std::optional<std::string> font;
    /// Legacy four-metric completeness; explicit face selection has its own snapshot gate.
    [[nodiscard]] bool complete() const { return fontSize && lineHeight && letterSpacing && align; }
    friend bool operator==(const TextStyle&, const TextStyle&) = default;
};

/// A glyph of one face at fixed axis values.
struct GlyphKey {
    std::uint32_t instance = 0;
    std::uint32_t glyph = 0;
    friend bool operator==(const GlyphKey&, const GlyphKey&) = default;
};

struct TextGlyph {
    Rect box;
    GlyphKey key;
};

struct TextLayout {
    double width = 0.0;
    double height = 0.0;
    std::vector<TextGlyph> glyphs;
};

/// Strict UTF-8, Latin-1 plus the documented punctuation set; no silent missing-glyph fallback.
[[nodiscard]] bool validText(const TextRun& text);
/// Why `validText` refuses `text`, or null when it accepts it.
[[nodiscard]] const char* textProblem(const TextRun& text);
/// Optional greedy wrapping at spaces, with glyph breaks for words wider than the available width.
[[nodiscard]] TextLayout layoutText(const TextRun& text, double availableWidth = -1.0, bool wrap = false);
/// Id of the bundled Inter variable face.
inline constexpr std::string_view kEmbeddedInterFace = "embedded/inter";
/// Registers `bytes` as face `id` when their SHA-256 is `sha256` in lowercase hex. Returns null, or why
/// it refused. Registering the same id and hash again succeeds without copying.
[[nodiscard]] const char* registerFontFace(std::string_view id, std::span<const std::uint8_t> bytes, std::string_view sha256);
/// SHA-256 of face `id`, or empty when no face has that id.
[[nodiscard]] std::string fontFaceSha256(std::string_view id);

/// Normalized atlas rectangle of `key`, rasterized on first use. Empty when the glyph has no ink.
[[nodiscard]] Rect textGlyphUv(GlyphKey key);
/// Grayscale glyph atlas, `kTextAtlasSize` pixels square.
[[nodiscard]] std::span<const std::uint8_t> textAtlas();
/// Grows when the atlas is cleared to make room, which invalidates earlier `textGlyphUv` results.
[[nodiscard]] std::uint64_t textAtlasGeneration();
struct TextAtlasRegion {
    int x = 0, y = 0, w = 0, h = 0;
};
/// Pixels changed since the last call, aligned to `kTextAtlasPadding`, or null when none changed.
[[nodiscard]] std::optional<TextAtlasRegion> takeTextAtlasChanges();
inline constexpr int kTextAtlasSize = 4096;
/// Every glyph's atlas rectangle has at least this many transparent atlas pixels around it, and cells are
/// aligned to it, so the first `kTextAtlasMipLevels` mip levels keep glyphs apart.
inline constexpr int kTextAtlasPadding = 8;
inline constexpr int kTextAtlasMipLevels = 4;

} // namespace vc
