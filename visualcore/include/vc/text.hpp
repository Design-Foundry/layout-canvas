#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <optional>
#include <string_view>
#include <vector>
#include "vc/font.hpp"
#include "vc/math.hpp"

namespace vc {

enum class TextAlign { Left, Center, Right };

/// A single face and style for an editable text node.
struct TextRun {
    std::string content;
    Id stack = kInterStackId;
    /// Axis values over the primary font's defaults, such as wght and slnt.
    FontAxes axes;
    FontFeatures features;
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
    std::optional<Id> stack;
    std::optional<FontAxes> axes;
    std::optional<FontFeatures> features;
    /// Legacy four-metric completeness; font selection has its own snapshot gate.
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
    /// Some characters come from the bundled face of the stack's generic family instead of a font
    /// of the stack.
    bool approximated = false;
    /// Some characters draw the missing-glyph mark.
    bool missing = false;
};

/// Strict UTF-8 without control characters other than newline. Checks the font settings' shape, not
/// that the stack exists or allows them.
[[nodiscard]] bool validText(const TextRun& text);
/// Why `validText` refuses `text`, or null when it accepts it.
[[nodiscard]] const char* textProblem(const TextRun& text);
/// The fonts of a text's stack in order and the generic family that ends it.
struct TextFonts {
    std::vector<const Font*> fonts;
    GenericFallback fallback = GenericFallback::SansSerif;
};
/// Optional greedy wrapping at spaces, with glyph breaks for words wider than the available width.
/// Each character comes from the first font that covers it, skipping fonts whose face is not
/// registered, then from the generic family's bundled face. A character nothing covers draws the
/// first available font's missing-glyph mark. Kerning applies only between glyphs of one font.
[[nodiscard]] TextLayout layoutText(const TextRun& text, const TextFonts& fonts, double availableWidth = -1.0, bool wrap = false);
/// Lays out text on the embedded Inter stack, whatever stack it names.
[[nodiscard]] TextLayout layoutText(const TextRun& text, double availableWidth = -1.0, bool wrap = false);
/// Id of the bundled Inter variable face.
inline constexpr std::string_view kEmbeddedInterFace = "embedded/inter";
/// Registers `bytes` as face `id` when their SHA-256 is `sha256` in lowercase hex. Returns null, or why
/// it refused. Registering the same id and hash again succeeds without copying.
[[nodiscard]] const char* registerFontFace(std::string_view id, std::span<const std::uint8_t> bytes, std::string_view sha256);
/// SHA-256 of face `id`, or empty when no face has that id.
[[nodiscard]] std::string fontFaceSha256(std::string_view id);
/// One file of a font family: the face that holds its bytes and the axis values only it answers,
/// such as ital 1, or wght 700 in a family of static files.
struct FontFile {
    std::string face;
    FontAxes fixed;
    friend bool operator==(const FontFile&, const FontFile&) = default;
};
/// Declares which files make up `provider`/`key` at `version`. A text on that font draws from the
/// file whose fixed axes are closest to its own values, once that file's face is registered.
/// Returns null, or why it refused. Declaring the same files again succeeds.
[[nodiscard]] const char* registerFontFamily(std::string_view provider, std::string_view key, std::string_view version,
                                             std::vector<FontFile> files);
/// Grows whenever a face or family is registered, which can change any text's layout.
[[nodiscard]] std::uint64_t fontRegistryGeneration();
/// What font bytes offer, as `{ axes: [{tag, min, max, default}], features }`, without registering
/// them. Null when the bytes are not a usable OpenType face.
[[nodiscard]] std::optional<std::string> fontCapabilitiesJson(std::span<const std::uint8_t> bytes);

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
