#include "vc/text.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <mutex>
#include <optional>
#include <unordered_map>
#include "fonts.hpp"

namespace vc {
namespace {
using fonts::kAtlasEm;

struct Character {
    std::uint32_t codepoint = 0;
    std::size_t offset = 0;
};

bool decode(std::string_view content, std::vector<Character>& out) {
    if (content.size() > 4096) return false;
    for (std::size_t i = 0; i < content.size();) {
        const std::size_t offset = i;
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
        if ((cp < 0x20 && cp != '\n') || (cp >= 0x7f && cp <= 0x9f)) return false;
        out.push_back({cp, offset});
        if (out.size() > 2048) return false;
    }
    return true;
}

struct Face {
    std::uint32_t instance = 0;
    bool oblique = false;
};

/// `font` at the text's axis values, each clamped to what the font allows, or nothing when the
/// font's face is not registered.
std::optional<Face> faceFor(const TextRun& text, const Font& font) {
    std::vector<fonts::Variation> axes;
    bool oblique = false;
    for (const auto& axis : font.axes) {
        const auto set = text.axes.find(axis.tag);
        std::optional<double> value = set != text.axes.end() ? std::optional<double>(set->second) : axis.defaultValue;
        if (!value) continue;
        const auto& domain = axis.domain;
        if (domain.values.empty()) value = std::clamp(*value, domain.min, domain.max);
        else value = *std::min_element(domain.values.begin(), domain.values.end(),
                                       [&](double a, double b) { return std::abs(a - *value) < std::abs(b - *value); });
        axes.push_back({fonts::tag(axis.tag), float(*value)});
        if ((axis.tag == "slnt" && *value != 0) || (axis.tag == "ital" && *value > 0)) oblique = true;
    }
    std::string face = font.face();
    if (const auto files = fonts::familyFiles(font.provider, font.key, font.familyVersion)) {
        const auto wanted = [&](const std::string& tag) {
            const auto set = text.axes.find(tag);
            if (set != text.axes.end()) return set->second;
            const auto* axis = font.axis(tag);
            return axis && axis->defaultValue ? *axis->defaultValue : 0.0;
        };
        double best = std::numeric_limits<double>::infinity();
        for (const auto& file : *files) {
            double distance = 0;
            for (const auto& [tag, value] : file.fixed) distance += std::abs(wanted(tag) - value) * (tag == "ital" ? 1e6 : 1.0);
            if (distance < best) { best = distance; face = file.face; }
        }
    }
    const int instance = fonts::instanceIndex(face, axes);
    if (instance < 0) return std::nullopt;
    return Face{std::uint32_t(instance), oblique};
}

std::vector<fonts::ShapedGlyph> shapeRuns(std::string_view paragraph, const std::vector<Face>& faces, const FontFeatures& features);

/// One paragraph shaped in runs of the face that covers each character, cached by faces, features
/// and text.
std::vector<fonts::ShapedGlyph> shapeParagraph(std::string_view paragraph, const std::vector<Face>& faces, const FontFeatures& features) {
    static std::mutex lock;
    static std::unordered_map<std::string, std::vector<fonts::ShapedGlyph>> cache;
    std::string key;
    for (const auto& face : faces) key += std::to_string(face.instance) + ',';
    for (const auto& [tag, value] : features)
        key += tag + '=' + std::to_string(std::holds_alternative<bool>(value) ? int(std::get<bool>(value)) : std::get<int>(value)) + ',';
    key += '\x1f';
    key += paragraph;
    {
        const std::scoped_lock guard(lock);
        if (const auto found = cache.find(key); found != cache.end()) return found->second;
    }
    auto shaped = shapeRuns(paragraph, faces, features);
    const std::scoped_lock guard(lock);
    if (cache.size() >= 4096) cache.clear();
    cache.emplace(std::move(key), shaped);
    return shaped;
}

std::vector<fonts::ShapedGlyph> shapeRuns(std::string_view paragraph, const std::vector<Face>& faces, const FontFeatures& features) {
    std::vector<Character> characters;
    decode(paragraph, characters);
    std::vector<std::size_t> chosen(characters.size(), 0);
    for (std::size_t i = 0; i < characters.size(); ++i) {
        const auto cp = characters[i].codepoint;
        if (i > 0 && fonts::attachesToPrevious(cp) && fonts::covers(faces[chosen[i - 1]].instance, cp)) {
            chosen[i] = chosen[i - 1];
            continue;
        }
        for (std::size_t f = 0; f < faces.size(); ++f)
            if (fonts::covers(faces[f].instance, cp)) { chosen[i] = f; break; }
    }
    std::vector<fonts::ShapedGlyph> out;
    for (std::size_t begin = 0; begin < characters.size();) {
        std::size_t end = begin + 1;
        while (end < characters.size() && chosen[end] == chosen[begin]) ++end;
        const std::size_t from = characters[begin].offset;
        const std::size_t to = end < characters.size() ? characters[end].offset : paragraph.size();
        for (auto glyph : fonts::shape(faces[chosen[begin]].instance, paragraph.substr(from, to - from), features)) {
            glyph.cluster += std::uint32_t(from);
            out.push_back(glyph);
        }
        begin = end;
    }
    return out;
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
    std::vector<Character> characters;
    if (!decode(text.content, characters)) return "content must be UTF-8 of at most 4096 bytes and 2048 characters, without control characters other than newline";
    return nullptr;
}

bool validText(const TextRun& text) {
    return !textProblem(text);
}

TextLayout layoutText(const TextRun& text, double availableWidth, bool wrap) {
    return layoutText(text, TextFonts{{&embeddedInterFont()}, GenericFallback::SansSerif}, availableWidth, wrap);
}

TextLayout layoutText(const TextRun& text, const TextFonts& stack, double availableWidth, bool wrap) {
    TextLayout result;
    if (!validText(text)) return result;
    std::vector<Face> faces;
    for (const Font* font : stack.fonts)
        if (font) if (const auto face = faceFor(text, *font)) faces.push_back(*face);
    if (const Font* generic = genericFont(stack.fallback))
        if (const auto face = faceFor(text, *generic)) faces.push_back(*face);
    if (faces.empty()) faces.push_back(*faceFor(text, embeddedInterFont()));
    std::unordered_map<std::uint32_t, bool> oblique;
    for (const auto& face : faces) oblique[face.instance] = face.oblique;
    const auto metrics = fonts::metrics(faces.front().instance);
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
        const auto& glyphs = shaped.emplace_back(shapeParagraph(paragraphs[p], faces, text.features));
        std::size_t begin = 0, i = 0, lastBreak = 0;
        double measured = 0, left = 0, right = 0;
        while (i < glyphs.size()) {
            const auto& glyph = glyphs[i];
            const double origin = measured + (i > begin ? glyphs[i - 1].kern * scale + text.letterSpacing : 0);
            const double next = origin + glyph.advance * scale;
            const double inkX = origin + (glyph.offsetX + glyph.ink.x) * scale;
            const bool slanted = oblique[glyph.instance];
            const double nextLeft = slanted && glyph.ink.w ? std::min(left, inkX) : left;
            const double nextRight = slanted && glyph.ink.w ? std::max(right, inkX + glyph.ink.w * scale) : right;
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
            if (oblique[glyph.instance] && glyph.ink.w) {
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
                {glyph.instance, glyph.glyph}});
            x += glyph.advance * scale;
        }
    }
    return result;
}

} // namespace vc
