#include "vc/icon.hpp"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include "fonts.hpp"

#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg/nanosvg.h"
#include "nanosvg/nanosvgrast.h"

namespace vc {
namespace {

constexpr const char* kStyleTag = "style";
constexpr const char* kFillTag = "FILL";
constexpr const char* kWeightTag = "wght";
constexpr const char* kOpticalTag = "opsz";

/// Where the built-in set's SVGs come from: google/material-design-icons at this commit, `symbols/web`.
constexpr const char* kMaterialSymbolsCommit = "10ecc14790612ca8fd164e3f4ffe9c16fc50d5b7";

IconSet makeMaterialSymbols() {
    IconSet set;
    set.id = kMaterialSymbolsSetId;
    set.identifier = "material-symbols";
    set.displayName = "Material Symbols";
    set.version = kMaterialSymbolsCommit;
    set.axes.push_back({kStyleTag, {std::string{"outlined"}, std::string{"rounded"}, std::string{"sharp"}}, std::string{"outlined"}});
    set.axes.push_back({kFillTag, {0.0, 1.0}, 0.0});
    set.axes.push_back({kWeightTag, {100.0, 200.0, 300.0, 400.0, 500.0, 600.0, 700.0}, 400.0});
    set.axes.push_back({kOpticalTag, {20.0, 24.0, 40.0, 48.0}, 24.0});
    return set;
}

bool sameText(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
    });
}

const IconAxis* axisNamed(const IconSet& set, std::string_view tag) {
    for (const auto& axis : set.axes) if (sameText(axis.tag, tag)) return &axis;
    return nullptr;
}

bool allowed(const IconAxis& axis, const IconAxisValue& value) {
    return std::find(axis.values.begin(), axis.values.end(), value) != axis.values.end();
}

std::string axisText(const IconAxisValue& value) {
    if (const auto* name = std::get_if<std::string>(&value)) return *name;
    const double number = std::get<double>(value);
    return std::to_string(static_cast<long long>(std::llround(number)));
}

} // namespace

const IconAxis* IconSet::axis(std::string_view tag) const { return axisNamed(*this, tag); }

const IconSet& materialSymbolsSet() {
    static const IconSet set = makeMaterialSymbols();
    return set;
}

const std::vector<IconSet>& builtinIconSets() {
    static const std::vector<IconSet> sets{materialSymbolsSet()};
    return sets;
}

const IconSet* findIconSet(const Id& id) {
    for (const auto& set : builtinIconSets()) if (set.id == id) return &set;
    return nullptr;
}

const IconSet* findIconSetByIdentifier(std::string_view identifier) {
    for (const auto& set : builtinIconSets()) if (set.identifier == identifier) return &set;
    return nullptr;
}

bool validIconGlyph(std::string_view glyph) {
    return !glyph.empty() && glyph.size() <= 64 && std::all_of(glyph.begin(), glyph.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    });
}

const char* iconProblem(const IconRef& ref) {
    const IconSet* set = findIconSet(ref.set);
    if (!set) return "set must name an icon set of the document";
    if (!validIconGlyph(ref.glyph)) return "glyph must be lowercase letters, digits and underscores, up to 64 characters";
    for (const auto& [tag, value] : ref.axes) {
        const IconAxis* axis = set->axis(tag);
        if (!axis || axis->tag != tag) return "axes names an axis the set does not have";
        if (!allowed(*axis, value)) return "axes has a value the set does not offer for that axis";
    }
    return nullptr;
}

const char* normalizeIcon(IconRef& ref) {
    const IconSet* set = findIconSet(ref.set);
    if (!set) return "set must name an icon set of the document";
    IconAxes next;
    for (auto& [tag, value] : ref.axes) {
        const IconAxis* axis = set->axis(tag);
        if (!axis) return "axes names an axis the set does not have";
        if (!allowed(*axis, value)) return "axes has a value the set does not offer for that axis";
        if (value != axis->defaultValue) next[axis->tag] = value;
    }
    ref.axes = std::move(next);
    return iconProblem(ref);
}

IconAxes resolvedIconAxes(const IconRef& ref) {
    IconAxes out;
    if (const IconSet* set = findIconSet(ref.set)) {
        for (const auto& axis : set->axes) {
            const auto given = ref.axes.find(axis.tag);
            out[axis.tag] = given != ref.axes.end() ? given->second : axis.defaultValue;
        }
    }
    return out;
}

std::string iconPath(const IconRef& ref) {
    const IconSet* set = findIconSet(ref.set);
    std::string out = (set ? set->identifier : std::string{"unknown"}) + "/" + ref.glyph;
    char separator = '?';
    if (set) for (const auto& axis : set->axes) {
        const auto found = ref.axes.find(axis.tag);
        if (found == ref.axes.end() || found->second == axis.defaultValue) continue;
        out += separator;
        out += axis.tag + "=" + axisText(found->second);
        separator = '&';
    }
    return out;
}

std::string iconSourceKey(const IconRef& ref) {
    const IconSet* set = findIconSet(ref.set);
    std::string out = (set ? set->identifier : std::string{"unknown"}) + "/" + ref.glyph;
    const auto axes = resolvedIconAxes(ref);
    char separator = '?';
    if (set) for (const auto& axis : set->axes) {
        out += separator;
        out += axis.tag + "=" + axisText(axes.at(axis.tag));
        separator = '&';
    }
    return out;
}

std::optional<IconRef> parseIconPath(std::string_view text, const char** problem) {
    const auto fail = [&](const char* why) -> std::optional<IconRef> {
        if (problem) *problem = why;
        return std::nullopt;
    };
    const auto question = text.find('?');
    const std::string_view head = text.substr(0, question);
    const auto slash = head.find('/');
    if (slash == std::string_view::npos || head.find('/', slash + 1) != std::string_view::npos)
        return fail("an icon path is <set>/<glyph>, such as material-symbols/arrow_back, with optional ?axis=value&axis=value");
    const IconSet* set = findIconSetByIdentifier(head.substr(0, slash));
    if (!set) return fail("the icon set of the path is unknown, use material-symbols");
    IconRef ref;
    ref.set = set->id;
    ref.glyph = std::string{head.substr(slash + 1)};
    if (!validIconGlyph(ref.glyph)) return fail("glyph must be lowercase letters, digits and underscores, up to 64 characters");
    if (question != std::string_view::npos) {
        std::string_view rest = text.substr(question + 1);
        while (!rest.empty()) {
            const auto amp = rest.find('&');
            const std::string_view pair = rest.substr(0, amp);
            rest = amp == std::string_view::npos ? std::string_view{} : rest.substr(amp + 1);
            const auto eq = pair.find('=');
            if (eq == std::string_view::npos) return fail("each modifier is axis=value");
            const IconAxis* axis = set->axis(pair.substr(0, eq));
            if (!axis) return fail("a modifier names an axis the set does not have, use style, fill, wght or opsz");
            if (ref.axes.contains(axis->tag)) return fail("an axis is given twice");
            const std::string_view raw = pair.substr(eq + 1);
            IconAxisValue value;
            if (std::holds_alternative<std::string>(axis->defaultValue)) {
                value = std::string{raw};
            } else {
                double number = 0;
                const auto parsed = std::from_chars(raw.data(), raw.data() + raw.size(), number);
                if (raw.empty() || parsed.ec != std::errc{} || parsed.ptr != raw.data() + raw.size()) return fail("a numeric axis takes a number");
                value = number;
            }
            if (!allowed(*axis, value)) return fail("a modifier has a value the set does not offer for that axis");
            ref.axes[axis->tag] = value;
        }
    }
    if (const char* why = normalizeIcon(ref)) return fail(why);
    return ref;
}

// ---------------------------------------------------------------------------
// SVG subset
// ---------------------------------------------------------------------------
namespace {

constexpr int kMaxSvgElements = 512;
constexpr int kMaxSvgDepth = 16;
constexpr int kMaxSvgAttributes = 32;

bool oneOf(std::string_view name, std::initializer_list<std::string_view> names) {
    return std::find(names.begin(), names.end(), name) != names.end();
}

bool nameChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == ':' || c == '_' || c == '-' || c == '.';
}

bool blank(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

bool allowedElement(std::string_view name) {
    return oneOf(name, {"svg", "g", "path", "rect", "circle", "ellipse", "line", "polyline", "polygon", "title", "desc"});
}

bool allowedAttribute(std::string_view name) {
    return oneOf(name, {"xmlns", "xmlns:xlink", "version", "width", "height", "viewBox", "preserveAspectRatio", "id", "class",
                        "x", "y", "rx", "ry", "cx", "cy", "r", "x1", "y1", "x2", "y2", "points", "d", "transform", "fill",
                        "fill-rule", "fill-opacity", "clip-rule", "opacity", "stroke", "stroke-width", "stroke-linecap",
                        "stroke-linejoin", "stroke-miterlimit", "stroke-opacity", "stroke-dasharray", "stroke-dashoffset",
                        "style", "role", "focusable", "aria-hidden"});
}

bool unsafeValue(std::string_view value) {
    if (value.find('&') != std::string_view::npos || value.find('<') != std::string_view::npos) return true;
    std::string lower(value);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return lower.find("url(") != std::string::npos || lower.find("@import") != std::string::npos ||
           lower.find("javascript:") != std::string::npos || lower.find("expression(") != std::string::npos;
}

} // namespace

const char* iconSvgProblem(std::string_view svg) {
    if (svg.empty()) return "the SVG is empty";
    if (svg.size() > kMaxIconSvgBytes) return "the SVG is larger than 64 KiB";
    std::vector<std::string> open;
    bool root = false, closedRoot = false;
    int elements = 0;
    std::size_t i = 0;
    const auto starts = [&](std::string_view prefix) { return svg.substr(i, prefix.size()) == prefix; };
    while (i < svg.size()) {
        if (svg[i] != '<') {
            if (!blank(svg[i]) && (open.empty() || (open.back() != "title" && open.back() != "desc")))
                return "the SVG has text outside title and desc";
            ++i;
            continue;
        }
        if (starts("<!--")) {
            const auto end = svg.find("-->", i + 4);
            if (end == std::string_view::npos) return "the SVG has an unterminated comment";
            i = end + 3;
            continue;
        }
        if (starts("<?")) {
            if (i != 0 && svg.substr(0, i).find_first_not_of(" \t\r\n") != std::string_view::npos) return "the SVG has a processing instruction after its start";
            const auto end = svg.find("?>", i + 2);
            if (end == std::string_view::npos) return "the SVG has an unterminated declaration";
            if (!starts("<?xml")) return "the SVG has a processing instruction other than the XML declaration";
            i = end + 2;
            continue;
        }
        if (starts("<!")) return "the SVG has a DOCTYPE, entity or CDATA section";
        if (starts("</")) {
            std::size_t j = i + 2;
            const std::size_t begin = j;
            while (j < svg.size() && nameChar(svg[j])) ++j;
            const std::string name(svg.substr(begin, j - begin));
            while (j < svg.size() && blank(svg[j])) ++j;
            if (j >= svg.size() || svg[j] != '>') return "the SVG has a malformed closing tag";
            if (open.empty() || open.back() != name) return "the SVG has mismatched tags";
            open.pop_back();
            if (open.empty()) closedRoot = true;
            i = j + 1;
            continue;
        }
        std::size_t j = i + 1;
        const std::size_t begin = j;
        while (j < svg.size() && nameChar(svg[j])) ++j;
        const std::string name(svg.substr(begin, j - begin));
        if (name.empty()) return "the SVG has a malformed tag";
        if (!allowedElement(name)) return "the SVG uses an element outside path, basic shapes, groups, title and desc";
        if (closedRoot) return "the SVG has content after its root";
        if (!root && name != "svg") return "the root element must be svg";
        if (root && name == "svg") return "the SVG has a nested svg element";
        root = true;
        if (++elements > kMaxSvgElements) return "the SVG has more than 512 elements";
        int attributes = 0;
        bool selfClosing = false;
        for (;;) {
            while (j < svg.size() && blank(svg[j])) ++j;
            if (j >= svg.size()) return "the SVG has an unterminated tag";
            if (svg[j] == '>') { ++j; break; }
            if (svg[j] == '/' && j + 1 < svg.size() && svg[j + 1] == '>') { selfClosing = true; j += 2; break; }
            const std::size_t nameBegin = j;
            while (j < svg.size() && nameChar(svg[j])) ++j;
            const std::string attribute(svg.substr(nameBegin, j - nameBegin));
            if (attribute.empty()) return "the SVG has a malformed attribute";
            if (++attributes > kMaxSvgAttributes) return "an SVG element has more than 32 attributes";
            if (!allowedAttribute(attribute) && attribute.rfind("data-", 0) != 0) return "the SVG uses an attribute outside geometry, fill and stroke";
            while (j < svg.size() && blank(svg[j])) ++j;
            if (j >= svg.size() || svg[j] != '=') return "the SVG has an attribute without a value";
            ++j;
            while (j < svg.size() && blank(svg[j])) ++j;
            if (j >= svg.size() || (svg[j] != '"' && svg[j] != '\'')) return "the SVG has an unquoted attribute value";
            const char quote = svg[j++];
            const auto end = svg.find(quote, j);
            if (end == std::string_view::npos) return "the SVG has an unterminated attribute value";
            if (unsafeValue(svg.substr(j, end - j))) return "the SVG has an attribute value with a reference, entity or script";
            j = end + 1;
        }
        if (!selfClosing) {
            if (static_cast<int>(open.size()) >= kMaxSvgDepth) return "the SVG nests deeper than 16 elements";
            open.push_back(name);
        } else if (open.empty()) {
            closedRoot = true;
        }
        i = j;
    }
    if (!root || !open.empty()) return "the SVG is not well formed";
    return nullptr;
}

std::optional<IconMask> rasterizeIconSvg(std::string_view svg, int side) {
    if (side < 1 || side > kMaxIconRasterSide || iconSvgProblem(svg)) return std::nullopt;
    std::string buffer(svg);
    NSVGimage* image = nsvgParse(buffer.data(), "px", 96.0f);
    if (!image) return std::nullopt;
    std::optional<IconMask> out;
    if (image->width > 0 && image->height > 0 && image->width < 1e5f && image->height < 1e5f) {
        NSVGrasterizer* rasterizer = nsvgCreateRasterizer();
        if (rasterizer) {
            const float scale = float(side) / std::max(image->width, image->height);
            const float tx = (float(side) - image->width * scale) / 2, ty = (float(side) - image->height * scale) / 2;
            std::vector<unsigned char> rgba(std::size_t(side) * side * 4, 0);
            nsvgRasterize(rasterizer, image, tx, ty, scale, rgba.data(), side, side, side * 4);
            IconMask mask;
            mask.side = side;
            mask.coverage.resize(std::size_t(side) * side);
            for (std::size_t p = 0; p < mask.coverage.size(); ++p) mask.coverage[p] = rgba[p * 4 + 3];
            out = std::move(mask);
            nsvgDeleteRasterizer(rasterizer);
        }
    }
    nsvgDelete(image);
    return out;
}

// ---------------------------------------------------------------------------
// Registry and atlas
// ---------------------------------------------------------------------------
namespace {

struct Registry {
    std::mutex lock;
    std::unordered_map<std::string, std::string> svgs;
    std::uint64_t generation = 0;
};

Registry& registry() {
    static Registry instance;
    return instance;
}

constexpr std::size_t kMaxRegisteredIcons = 8192;

} // namespace

const char* registerIconSvg(std::string_view key, std::string_view svg) {
    if (key.empty() || key.size() > 256) return "an icon key has 1 to 256 characters";
    if (const char* why = iconSvgProblem(svg)) return why;
    auto& r = registry();
    const std::scoped_lock guard(r.lock);
    const auto found = r.svgs.find(std::string(key));
    if (found != r.svgs.end()) return found->second == svg ? nullptr : "an icon with this key has different SVG";
    if (r.svgs.size() >= kMaxRegisteredIcons) return "too many icons are registered";
    r.svgs.emplace(std::string(key), std::string(svg));
    ++r.generation;
    return nullptr;
}

bool iconRegistered(std::string_view key) {
    auto& r = registry();
    const std::scoped_lock guard(r.lock);
    return r.svgs.contains(std::string(key));
}

std::uint64_t iconRegistryGeneration() {
    auto& r = registry();
    const std::scoped_lock guard(r.lock);
    return r.generation;
}

int iconRasterSide(double drawnPixels) {
    int side = 16;
    while (side < kMaxIconRasterSide && side < drawnPixels) side *= 2;
    return side;
}

Rect iconGlyphUv(std::string_view key, int side) {
    if (side < 1 || side > kMaxIconRasterSide) return {};
    const std::string name = std::string(key) + "@" + std::to_string(side);
    if (const auto cached = fonts::atlasFind(name)) return *cached;
    std::string svg;
    {
        auto& r = registry();
        const std::scoped_lock guard(r.lock);
        const auto found = r.svgs.find(std::string(key));
        if (found == r.svgs.end()) return {};
        svg = found->second;
    }
    const auto mask = rasterizeIconSvg(svg, side);
    if (!mask || std::none_of(mask->coverage.begin(), mask->coverage.end(), [](std::uint8_t c) { return c != 0; })) return {};
    return fonts::atlasPlace(name, side, side, mask->coverage);
}

} // namespace vc
