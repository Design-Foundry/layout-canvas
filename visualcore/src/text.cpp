#include "vc/text.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include "fonts.hpp"

namespace vc {
namespace {
using fonts::kAtlasEm;
constexpr std::uint32_t kPunctuation[] = {0x2013, 0x2014, 0x2018, 0x2019, 0x201a, 0x201c, 0x201d,
                                        0x201e, 0x2022, 0x2026, 0x2039, 0x203a, 0x20ac, 0x2122};

bool decode(std::string_view content, std::vector<std::uint32_t>& out) {
    if (content.size() > 4096) return false;
    for (std::size_t i = 0; i < content.size();) {
        auto c = static_cast<unsigned char>(content[i++]);
        std::uint32_t cp = c;
        int more = 0;
        if (c < 0x80) {}
        else if (c >= 0xc2 && c <= 0xdf) { cp = c & 0x1f; more = 1; }
        else if (c >= 0xe0 && c <= 0xef) { cp = c & 0x0f; more = 2; }
        else if (c >= 0xf0 && c <= 0xf4) { cp = c & 7; more = 3; }
        else return false;
        if (i + more > content.size()) return false;
        for (int j = 0; j < more; ++j) {
            const auto next = static_cast<unsigned char>(content[i++]);
            if ((next & 0xc0) != 0x80) return false;
            cp = (cp << 6) | (next & 0x3f);
        }
        if ((more == 1 && cp < 0x80) || (more == 2 && cp < 0x800) || (more == 3 && cp < 0x10000) ||
            cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
        if (cp != '\n' && !(cp >= 0x20 && cp <= 0x7e) && !(cp >= 0xa0 && cp <= 0xff) &&
            std::find(std::begin(kPunctuation), std::end(kPunctuation), cp) == std::end(kPunctuation)) return false;
        out.push_back(cp);
        if (out.size() > 2048) return false;
    }
    return true;
}

struct Face {
    int instance = -1;
    bool oblique = false;
};

Face faceFor(const TextRun& text, const Font& primary) {
    std::vector<fonts::Variation> axes;
    bool oblique = false;
    for (const auto& axis : primary.axes) {
        const auto set = text.axes.find(axis.tag);
        const auto value = set != text.axes.end() ? std::optional<double>(set->second) : axis.defaultValue;
        if (!value) continue;
        axes.push_back({fonts::tag(axis.tag), float(*value)});
        if ((axis.tag == "slnt" && *value != 0) || (axis.tag == "ital" && *value > 0)) oblique = true;
    }
    return {fonts::instanceIndex(primary.face(), axes), oblique};
}

struct Line {
    std::size_t paragraph = 0, begin = 0, end = 0;
};
} // namespace

const char* textProblem(const TextRun& text) {
    if (text.stack.isNil()) return "stack must name a font stack";
    if (const char* why = fontSettingsProblem(text.axes, text.features)) return why;
    if (!std::isfinite(text.fontSize) || text.fontSize < 1 || text.fontSize > 512) return "fontSize must be between 1 and 512";
    if (!std::isfinite(text.lineHeight) || text.lineHeight < 1 || text.lineHeight > 4) return "lineHeight must be between 1 and 4";
    if (!std::isfinite(text.letterSpacing) || text.letterSpacing < 0 || text.letterSpacing > 128) return "letterSpacing must be between 0 and 128";
    if (text.align != TextAlign::Left && text.align != TextAlign::Center && text.align != TextAlign::Right) return "align must be left, center or right";
    std::vector<std::uint32_t> codepoints;
    if (!decode(text.content, codepoints)) return "content must be UTF-8 using Latin characters the bundled fonts cover";
    return nullptr;
}

bool validText(const TextRun& text) {
    return !textProblem(text);
}

TextLayout layoutText(const TextRun& text, double availableWidth, bool wrap) {
    if (text.stack == kInterStackId) return layoutText(text, embeddedInterFont(), availableWidth, wrap);
    TextLayout result;
    result.height = text.fontSize * text.lineHeight;
    return result;
}

TextLayout layoutText(const TextRun& text, const Font& primary, double availableWidth, bool wrap) {
    TextLayout result;
    if (!validText(text)) return result;
    const auto face = faceFor(text, primary);
    if (face.instance < 0) {
        result.height = text.fontSize * text.lineHeight * double(1 + std::count(text.content.begin(), text.content.end(), '\n'));
        return result;
    }
    const auto instance = std::uint32_t(face.instance);
    const auto metrics = fonts::metrics(instance);
    const double scale = text.fontSize / kAtlasEm;
    const double lineHeight = text.fontSize * text.lineHeight;
    const double baseline = (lineHeight - (metrics.ascent - metrics.descent) * scale) / 2 + metrics.ascent * scale;
    std::vector<std::string_view> paragraphs;
    const std::string_view content = text.content;
    for (std::size_t start = 0;;) {
        const auto end = content.find('\n', start);
        paragraphs.push_back(content.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    std::vector<std::vector<fonts::ShapedGlyph>> shaped;
    std::vector<Line> lines;
    for (std::size_t p = 0; p < paragraphs.size(); ++p) {
        const auto& glyphs = shaped.emplace_back(fonts::shape(instance, paragraphs[p], text.features));
        std::size_t begin = 0, i = 0, lastBreak = 0;
        double measured = 0, left = 0, right = 0;
        while (i < glyphs.size()) {
            const auto& glyph = glyphs[i];
            const double origin = measured + (i > begin ? glyphs[i - 1].kern * scale + text.letterSpacing : 0);
            const double next = origin + glyph.advance * scale;
            const double inkX = origin + (glyph.offsetX + glyph.ink.x) * scale;
            const double nextLeft = face.oblique && glyph.ink.w ? std::min(left, inkX) : left;
            const double nextRight = face.oblique && glyph.ink.w ? std::max(right, inkX + glyph.ink.w * scale) : right;
            if (wrap && availableWidth >= 0 && std::max(next, nextRight) - nextLeft > availableWidth && i > begin) {
                const auto end = lastBreak > begin ? lastBreak : i;
                lines.push_back({p, begin, end});
                begin = i = end; lastBreak = begin; measured = left = right = 0;
                continue;
            }
            measured = next; left = nextLeft; right = nextRight; ++i;
            if (paragraphs[p][glyph.cluster] == ' ') lastBreak = i;
        }
        lines.push_back({p, begin, glyphs.size()});
    }
    std::vector<double> widths, bearings;
    for (const auto& line : lines) {
        const auto& glyphs = shaped[line.paragraph];
        double width = 0, left = 0, right = 0;
        for (auto i = line.begin; i < line.end; ++i) {
            if (i > line.begin) width += glyphs[i - 1].kern * scale + text.letterSpacing;
            const auto& glyph = glyphs[i];
            if (face.oblique && glyph.ink.w) {
                left = std::min(left, width + (glyph.offsetX + glyph.ink.x) * scale);
                right = std::max(right, width + (glyph.offsetX + glyph.ink.x + glyph.ink.w) * scale);
            }
            width += glyph.advance * scale;
        }
        widths.push_back(std::max(width, right) - left);
        bearings.push_back(-left);
    }
    result.width = std::max(0.0, *std::max_element(widths.begin(), widths.end()));
    result.height = double(widths.size()) * lineHeight;
    const double width = availableWidth < 0 ? result.width : availableWidth;
    const double factor = text.align == TextAlign::Center ? .5 : text.align == TextAlign::Right ? 1 : 0;
    for (std::size_t row = 0; row < lines.size(); ++row) {
        const auto& line = lines[row];
        const auto& glyphs = shaped[line.paragraph];
        double x = (width - widths[row]) * factor + bearings[row];
        for (auto i = line.begin; i < line.end; ++i) {
            if (i > line.begin) x += glyphs[i - 1].kern * scale + text.letterSpacing;
            const auto& glyph = glyphs[i];
            if (glyph.ink.w && glyph.ink.h) result.glyphs.push_back({{x + (glyph.offsetX + glyph.ink.x) * scale,
                double(row) * lineHeight + baseline + (glyph.ink.y - glyph.offsetY) * scale, glyph.ink.w * scale, glyph.ink.h * scale},
                {instance, glyph.glyph}});
            x += glyph.advance * scale;
        }
    }
    return result;
}

} // namespace vc
