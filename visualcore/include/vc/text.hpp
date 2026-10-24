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

/// A single face and style for an editable text node. The font is embedded in both runtimes.
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

struct TextGlyph {
    Rect box;
    /// Normalized atlas rectangle, not a world-space box.
    Rect uv;
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
/// Immutable trusted font atlas; external font bytes are deliberately not accepted.
[[nodiscard]] std::span<const std::uint8_t> textAtlas();
inline constexpr int kTextAtlasSize = 4096;

} // namespace vc
