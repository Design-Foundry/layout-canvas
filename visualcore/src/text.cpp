#include "vc/text.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb/stb_truetype.h"

namespace vc {
namespace {
const unsigned char kFont[] = {
#include "vc_inter_font.inc"
};
const unsigned char kBoldFont[] = {
#include "vc_inter_bold_font.inc"
};
const unsigned char kObliqueFont[] = {
#include "vc_inter_oblique_font.inc"
};
const unsigned char kBoldObliqueFont[] = {
#include "vc_inter_boldoblique_font.inc"
};
constexpr double kAtlasEm = 96.0;
struct KernPair { std::uint32_t key; int advance; };
constexpr KernPair kKerning[] = {
#include "inter_kerning.inc"
};
constexpr KernPair kBoldKerning[] = {
#include "inter_bold_kerning.inc"
};
constexpr KernPair kObliqueKerning[] = {
#include "inter_oblique_kerning.inc"
};
constexpr KernPair kBoldObliqueKerning[] = {
#include "inter_bold_oblique_kerning.inc"
};
constexpr std::uint32_t kPunctuation[] = {0x2013, 0x2014, 0x2018, 0x2019, 0x201a, 0x201c, 0x201d,
                                        0x201e, 0x2022, 0x2026, 0x2039, 0x203a, 0x20ac, 0x2122};
struct Glyph {
    Rect ink;
    Rect uv;
    double advance = 0;
};

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

class Font {
public:
    Font(const unsigned char* data, std::span<const KernPair> pairs, std::vector<std::uint8_t>& pixels, int& x, int& y, int& rowHeight) : pairs_(pairs) {
        if (!stbtt_InitFont(&info, data, 0)) throw std::runtime_error("Invalid bundled font");
        const auto scale = stbtt_ScaleForMappingEmToPixels(&info, float(kAtlasEm));
        int top, bottom, gap;
        stbtt_GetFontVMetrics(&info, &top, &bottom, &gap);
        ascent = top * scale; descent = bottom * scale;
        std::vector<std::uint32_t> characters;
        for (std::uint32_t c = 0x20; c <= 0x7e; ++c) characters.push_back(c);
        for (std::uint32_t c = 0xa0; c <= 0xff; ++c) characters.push_back(c);
        characters.insert(characters.end(), std::begin(kPunctuation), std::end(kPunctuation));
        for (auto cp : characters) {
            if (!stbtt_FindGlyphIndex(&info, cp)) throw std::runtime_error("Bundled font lacks a declared glyph");
            int width, height, left, upper, advance, bearing;
            unsigned char* bitmap = stbtt_GetCodepointBitmap(&info, scale, scale, cp, &width, &height, &left, &upper);
            if (x + width + 1 > kTextAtlasSize) { x = 1; y += rowHeight + 2; rowHeight = 0; }
            if (y + height + 1 > kTextAtlasSize) { stbtt_FreeBitmap(bitmap, nullptr); throw std::runtime_error("Font atlas overflow"); }
            for (int j = 0; j < height; ++j) std::copy_n(bitmap + j * width, width, pixels.begin() + (y + j) * kTextAtlasSize + x);
            stbtt_FreeBitmap(bitmap, nullptr);
            stbtt_GetCodepointHMetrics(&info, cp, &advance, &bearing);
            glyphs.emplace(cp, Glyph{{double(left), double(upper), double(width), double(height)},
                {double(x) / kTextAtlasSize, double(y) / kTextAtlasSize, double(width) / kTextAtlasSize, double(height) / kTextAtlasSize}, advance * scale});
            x += width + 2; rowHeight = std::max(rowHeight, height);
        }
    }
    double kern(std::uint32_t a, std::uint32_t b) const {
        const std::uint32_t key = (a << 16) | b;
        const auto at = std::lower_bound(pairs_.begin(), pairs_.end(), key,
            [](const KernPair& pair, std::uint32_t value) { return pair.key < value; });
        return at != pairs_.end() && at->key == key ? at->advance * stbtt_ScaleForMappingEmToPixels(&info, float(kAtlasEm)) : 0.0;
    }
    stbtt_fontinfo info{};
    std::unordered_map<std::uint32_t, Glyph> glyphs;
    double ascent = 0, descent = 0;
private:
    std::span<const KernPair> pairs_;
};
struct FontSet {
    std::vector<std::uint8_t> pixels = std::vector<std::uint8_t>(kTextAtlasSize * kTextAtlasSize, 0);
    int x = 1, y = 1, rowHeight = 0;
    Font regular{kFont, kKerning, pixels, x, y, rowHeight};
    Font bold{kBoldFont, kBoldKerning, pixels, x, y, rowHeight};
    Font oblique{kObliqueFont, kObliqueKerning, pixels, x, y, rowHeight};
    Font boldOblique{kBoldObliqueFont, kBoldObliqueKerning, pixels, x, y, rowHeight};
};
const FontSet& fonts() { static const FontSet instance; return instance; }
} // namespace

bool validText(const TextRun& text) {
    std::vector<std::uint32_t> codepoints;
    return (text.font == "inter-regular-v1" || text.font == "inter-bold-v1" || text.font == "inter-oblique-v1" || text.font == "inter-bold-oblique-v1") && std::isfinite(text.fontSize) && text.fontSize >= 1 && text.fontSize <= 512 &&
        std::isfinite(text.lineHeight) && text.lineHeight >= 1 && text.lineHeight <= 4 &&
        std::isfinite(text.letterSpacing) && text.letterSpacing >= 0 && text.letterSpacing <= 128 &&
        (text.align == TextAlign::Left || text.align == TextAlign::Center || text.align == TextAlign::Right) && decode(text.content, codepoints);
}

TextLayout layoutText(const TextRun& text, double availableWidth, bool wrap) {
    TextLayout result;
    if (!validText(text)) return result;
    std::vector<std::uint32_t> cps; decode(text.content, cps);
    const bool oblique = text.font == "inter-oblique-v1" || text.font == "inter-bold-oblique-v1";
    const Font& face = text.font == "inter-bold-v1" ? fonts().bold : text.font == "inter-oblique-v1" ? fonts().oblique : text.font == "inter-bold-oblique-v1" ? fonts().boldOblique : fonts().regular;
    const double scale = text.fontSize / kAtlasEm;
    const double lineHeight = text.fontSize * text.lineHeight;
    const double baseline = (lineHeight - (face.ascent - face.descent) * scale) / 2 + face.ascent * scale;
    std::vector<std::vector<std::uint32_t>> lines;
    std::size_t begin = 0, i = 0, lastBreak = 0;
    double measured = 0;
    double left = 0, right = 0;
    std::uint32_t prior = 0;
    while (i < cps.size()) {
        const auto cp = cps[i];
        if (cp == '\n') {
            lines.emplace_back(cps.begin() + begin, cps.begin() + i);
            begin = ++i; lastBreak = begin; measured = left = right = 0; prior = 0; continue;
        }
        const auto& glyph = face.glyphs.at(cp);
        const double origin = measured + (prior ? face.kern(prior, cp) * scale + text.letterSpacing : 0);
        const double next = origin + glyph.advance * scale;
        const double nextLeft = oblique && glyph.ink.w ? std::min(left, origin + glyph.ink.x * scale) : left;
        const double nextRight = oblique && glyph.ink.w ? std::max(right, origin + (glyph.ink.x + glyph.ink.w) * scale) : right;
        if (wrap && availableWidth >= 0 && std::max(next, nextRight) - nextLeft > availableWidth && i > begin) {
            const auto end = lastBreak > begin ? lastBreak : i;
            lines.emplace_back(cps.begin() + begin, cps.begin() + end);
            begin = i = end; lastBreak = begin; measured = left = right = 0; prior = 0; continue;
        }
        measured = next; left = nextLeft; right = nextRight; prior = cp; ++i;
        if (cp == ' ') lastBreak = i;
    }
    lines.emplace_back(cps.begin() + begin, cps.end());
    std::vector<double> widths;
    std::vector<double> bearings;
    std::uint32_t previous = 0;
    for (const auto& line : lines) {
        widths.push_back(0); previous = 0; left = right = 0;
        for (auto cp : line) {
            if (previous) widths.back() += face.kern(previous, cp) * scale + text.letterSpacing;
            const auto& glyph = face.glyphs.at(cp);
            if (oblique && glyph.ink.w) {
                left = std::min(left, widths.back() + glyph.ink.x * scale);
                right = std::max(right, widths.back() + (glyph.ink.x + glyph.ink.w) * scale);
            }
            widths.back() += face.glyphs.at(cp).advance * scale;
            previous = cp;
        }
        widths.back() = std::max(widths.back(), right) - left;
        bearings.push_back(-left);
    }
    result.width = std::max(0.0, *std::max_element(widths.begin(), widths.end()));
    result.height = widths.size() * lineHeight;
    const double width = availableWidth < 0 ? result.width : availableWidth;
    const auto offset = [&](std::size_t i) { return (width - widths[i]) * (text.align == TextAlign::Center ? .5 : text.align == TextAlign::Right ? 1 : 0); };
    for (std::size_t row = 0; row < lines.size(); ++row) {
        double x = offset(row) + bearings[row]; previous = 0;
        for (auto cp : lines[row]) {
            const Glyph& glyph = face.glyphs.at(cp);
            if (previous) x += face.kern(previous, cp) * scale + text.letterSpacing;
            if (glyph.ink.w && glyph.ink.h) result.glyphs.push_back({{x + glyph.ink.x * scale,
                row * lineHeight + baseline + glyph.ink.y * scale, glyph.ink.w * scale, glyph.ink.h * scale}, glyph.uv});
            x += glyph.advance * scale; previous = cp;
        }
    }
    return result;
}

std::span<const std::uint8_t> textAtlas() { return fonts().pixels; }
} // namespace vc
