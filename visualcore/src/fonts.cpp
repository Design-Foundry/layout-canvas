#include "fonts.hpp"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <hb.h>
#include <hb-ot.h>
#include "sha256.hpp"
#include "vc/text.hpp"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb/stb_truetype.h"

namespace vc {
namespace {
const unsigned char kInter[] = {
#include "vc_inter_font.inc"
};
constexpr std::size_t kMaxFaceBytes = std::size_t(32) << 20;
constexpr std::size_t kShapeCacheLimit = 8192;

struct Face {
    std::string sha256;
    std::vector<std::uint8_t> bytes;
    hb_blob_t* blob = nullptr;
    hb_face_t* face = nullptr;
};

struct GlyphMetrics {
    int advance = 0;
    Rect ink;
};

struct Instance {
    hb_font_t* font = nullptr;
    float scale = 0;
    fonts::Metrics metrics;
    std::unordered_map<std::uint32_t, GlyphMetrics> glyphs;
};

constexpr int align(int size) { return (size + kTextAtlasPadding - 1) / kTextAtlasPadding * kTextAtlasPadding; }

bool lowercaseHex(std::string_view value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

int readInt16(hb_blob_t* blob, unsigned offset, int fallback) {
    unsigned length = 0;
    const auto* data = reinterpret_cast<const unsigned char*>(hb_blob_get_data(blob, &length));
    if (offset + 2 > length) return fallback;
    return std::int16_t(std::uint16_t(data[offset] << 8 | data[offset + 1]));
}

struct State {
    std::mutex lock;
    std::map<std::string, std::unique_ptr<Face>, std::less<>> faces;
    std::map<std::string, std::uint32_t, std::less<>> instanceIds;
    std::vector<std::unique_ptr<Instance>> instances;
    std::unordered_map<std::string, std::vector<fonts::ShapedGlyph>> shaped;
    std::vector<std::uint8_t> pixels = std::vector<std::uint8_t>(std::size_t(kTextAtlasSize) * kTextAtlasSize, 0);
    std::unordered_map<std::uint64_t, Rect> cells;
    int x = 0, y = 0, rowHeight = 0;
    std::uint64_t generation = 0;
    int dirtyX0 = INT_MAX, dirtyY0 = INT_MAX, dirtyX1 = 0, dirtyY1 = 0;

    State() {
        if (add(kEmbeddedInterFace, {kInter, sizeof kInter}, VC_INTER_SHA256, false)) std::abort();
    }

    const char* add(std::string_view id, std::span<const std::uint8_t> bytes, std::string_view sha256, bool verify) {
        if (id.empty() || id.size() > 256) return "face id must have 1 to 256 characters";
        if (!lowercaseHex(sha256)) return "sha256 must be 64 lowercase hexadecimal digits";
        if (const auto found = faces.find(id); found != faces.end())
            return found->second->sha256 == sha256 ? nullptr : "a face with this id has different bytes";
        if (bytes.empty() || bytes.size() > kMaxFaceBytes) return "font bytes must have 1 byte to 32 MiB";
        if (verify && sha256Hex(bytes) != sha256) return "font bytes do not match sha256";
        auto face = std::make_unique<Face>();
        face->sha256 = sha256;
        if (verify) face->bytes.assign(bytes.begin(), bytes.end());
        const auto* data = verify ? face->bytes.data() : bytes.data();
        face->blob = hb_blob_create(reinterpret_cast<const char*>(data), unsigned(bytes.size()), HB_MEMORY_MODE_READONLY, nullptr, nullptr);
        face->face = hb_face_create(face->blob, 0);
        const auto upem = hb_face_get_upem(face->face);
        if (hb_face_get_glyph_count(face->face) == 0 || upem < 16 || upem > 16384) {
            hb_face_destroy(face->face);
            hb_blob_destroy(face->blob);
            return "font bytes are not a usable OpenType face";
        }
        hb_face_make_immutable(face->face);
        faces.emplace(std::string(id), std::move(face));
        return nullptr;
    }

    GlyphMetrics& glyph(Instance& instance, std::uint32_t glyph) {
        auto [entry, inserted] = instance.glyphs.try_emplace(glyph);
        if (!inserted) return entry->second;
        entry->second.advance = hb_font_get_glyph_h_advance(instance.font, glyph);
        hb_glyph_extents_t e{};
        if (hb_font_get_glyph_extents(instance.font, glyph, &e) && e.width && e.height) {
            const float s = instance.scale;
            const int x0 = int(std::floor(float(e.x_bearing) * s)), x1 = int(std::ceil(float(e.x_bearing + e.width) * s));
            const int y0 = int(std::floor(-float(e.y_bearing) * s)), y1 = int(std::ceil(-float(e.y_bearing + e.height) * s));
            entry->second.ink = {double(x0), double(y0), double(x1 - x0), double(y1 - y0)};
        }
        return entry->second;
    }

    void markDirty(int left, int top, int right, int bottom) {
        dirtyX0 = std::min(dirtyX0, left); dirtyY0 = std::min(dirtyY0, top);
        dirtyX1 = std::max(dirtyX1, right); dirtyY1 = std::max(dirtyY1, bottom);
    }

    void clearAtlas() {
        std::fill(pixels.begin(), pixels.end(), std::uint8_t(0));
        cells.clear();
        x = y = rowHeight = 0;
        ++generation;
        markDirty(0, 0, kTextAtlasSize, kTextAtlasSize);
    }
};

State& state() {
    static State instance;
    return instance;
}

struct Outline {
    std::vector<stbtt_vertex> vertices;
};

stbtt_vertex vertex(unsigned char type, float x, float y, float cx = 0, float cy = 0, float cx1 = 0, float cy1 = 0) {
    stbtt_vertex out{};
    out.type = type;
    out.x = short(std::lround(x)); out.y = short(std::lround(y));
    out.cx = short(std::lround(cx)); out.cy = short(std::lround(cy));
    out.cx1 = short(std::lround(cx1)); out.cy1 = short(std::lround(cy1));
    return out;
}

hb_draw_funcs_t* outlineFuncs() {
    static hb_draw_funcs_t* const funcs = [] {
        auto* d = hb_draw_funcs_create();
        hb_draw_funcs_set_move_to_func(d, [](hb_draw_funcs_t*, void* o, hb_draw_state_t*, float x, float y, void*) {
            static_cast<Outline*>(o)->vertices.push_back(vertex(STBTT_vmove, x, y)); }, nullptr, nullptr);
        hb_draw_funcs_set_line_to_func(d, [](hb_draw_funcs_t*, void* o, hb_draw_state_t*, float x, float y, void*) {
            static_cast<Outline*>(o)->vertices.push_back(vertex(STBTT_vline, x, y)); }, nullptr, nullptr);
        hb_draw_funcs_set_quadratic_to_func(d, [](hb_draw_funcs_t*, void* o, hb_draw_state_t*, float cx, float cy, float x, float y, void*) {
            static_cast<Outline*>(o)->vertices.push_back(vertex(STBTT_vcurve, x, y, cx, cy)); }, nullptr, nullptr);
        hb_draw_funcs_set_cubic_to_func(d, [](hb_draw_funcs_t*, void* o, hb_draw_state_t*, float c1x, float c1y, float c2x, float c2y, float x, float y, void*) {
            static_cast<Outline*>(o)->vertices.push_back(vertex(STBTT_vcubic, x, y, c1x, c1y, c2x, c2y)); }, nullptr, nullptr);
        hb_draw_funcs_make_immutable(d);
        return d;
    }();
    return funcs;
}
} // namespace

namespace fonts {

std::uint32_t tag(std::string_view name) { return name.size() == 4 ? HB_TAG(name[0], name[1], name[2], name[3]) : 0; }

int instanceIndex(std::string_view faceId, std::span<const Variation> variations) {
    std::vector<Variation> sorted(variations.begin(), variations.end());
    std::sort(sorted.begin(), sorted.end(), [](const Variation& a, const Variation& b) { return a.tag < b.tag; });
    std::string key(faceId);
    for (const auto& v : sorted) {
        char part[48];
        std::snprintf(part, sizeof part, "|%08x=%a", unsigned(v.tag), double(v.value));
        key += part;
    }
    auto& s = state();
    const std::scoped_lock guard(s.lock);
    if (const auto found = s.instanceIds.find(key); found != s.instanceIds.end()) return int(found->second);
    const auto face = s.faces.find(faceId);
    if (face == s.faces.end()) return -1;
    auto instance = std::make_unique<Instance>();
    instance->font = hb_font_create(face->second->face);
    const auto upem = hb_face_get_upem(face->second->face);
    hb_font_set_scale(instance->font, int(upem), int(upem));
    std::vector<hb_variation_t> axes;
    for (const auto& v : sorted) axes.push_back({v.tag, v.value});
    hb_font_set_variations(instance->font, axes.data(), unsigned(axes.size()));
    hb_font_make_immutable(instance->font);
    instance->scale = float(kAtlasEm) / float(upem);
    hb_blob_t* hhea = hb_face_reference_table(face->second->face, HB_TAG('h', 'h', 'e', 'a'));
    hb_font_extents_t extents{};
    hb_font_get_h_extents(instance->font, &extents);
    const int ascent = readInt16(hhea, 4, extents.ascender) + int(std::lround(hb_ot_metrics_get_variation(instance->font, HB_OT_METRICS_TAG_HORIZONTAL_ASCENDER)));
    const int descent = readInt16(hhea, 6, extents.descender) + int(std::lround(hb_ot_metrics_get_variation(instance->font, HB_OT_METRICS_TAG_HORIZONTAL_DESCENDER)));
    hb_blob_destroy(hhea);
    instance->metrics = {double(float(ascent) * instance->scale), double(float(descent) * instance->scale)};
    const auto index = std::uint32_t(s.instances.size());
    s.instances.push_back(std::move(instance));
    s.instanceIds.emplace(std::move(key), index);
    return int(index);
}

Metrics metrics(std::uint32_t instance) {
    auto& s = state();
    const std::scoped_lock guard(s.lock);
    return s.instances.at(instance)->metrics;
}

std::vector<ShapedGlyph> shape(std::uint32_t instanceIndex, std::string_view text, const FontFeatures& features) {
    auto& s = state();
    std::string key = std::to_string(instanceIndex);
    std::vector<hb_feature_t> settings;
    for (const auto& [tag, value] : features) {
        const unsigned number = std::holds_alternative<bool>(value) ? unsigned(std::get<bool>(value)) : unsigned(std::get<int>(value));
        settings.push_back({fonts::tag(tag), number, HB_FEATURE_GLOBAL_START, HB_FEATURE_GLOBAL_END});
        key += '|' + tag + '=' + std::to_string(number);
    }
    key += '\x1f';
    key += text;
    hb_font_t* font = nullptr;
    {
        const std::scoped_lock guard(s.lock);
        if (const auto found = s.shaped.find(key); found != s.shaped.end()) return found->second;
        font = s.instances.at(instanceIndex)->font;
    }
    hb_buffer_t* buffer = hb_buffer_create();
    hb_buffer_add_utf8(buffer, text.data(), int(text.size()), 0, int(text.size()));
    hb_buffer_guess_segment_properties(buffer);
    hb_shape(font, buffer, settings.data(), unsigned(settings.size()));
    unsigned count = 0;
    const auto* info = hb_buffer_get_glyph_infos(buffer, &count);
    const auto* positions = hb_buffer_get_glyph_positions(buffer, &count);
    std::vector<ShapedGlyph> out(count);
    const std::scoped_lock guard(s.lock);
    auto& instance = *s.instances[instanceIndex];
    const float scale = instance.scale;
    for (unsigned i = 0; i < count; ++i) {
        const auto& metrics = s.glyph(instance, info[i].codepoint);
        out[i] = {info[i].codepoint, info[i].cluster, double(float(metrics.advance) * scale),
                  double(float(positions[i].x_advance - metrics.advance) * scale),
                  double(float(positions[i].x_offset) * scale), double(float(positions[i].y_offset) * scale), metrics.ink};
    }
    hb_buffer_destroy(buffer);
    if (s.shaped.size() >= kShapeCacheLimit) s.shaped.clear();
    s.shaped.emplace(std::move(key), out);
    return out;
}

std::optional<Capabilities> capabilities(std::string_view faceId) {
    auto& s = state();
    const std::scoped_lock guard(s.lock);
    const auto found = s.faces.find(faceId);
    if (found == s.faces.end()) return std::nullopt;
    hb_face_t* face = found->second->face;
    Capabilities out;
    unsigned count = hb_ot_var_get_axis_count(face);
    std::vector<hb_ot_var_axis_info_t> axes(count);
    hb_ot_var_get_axis_infos(face, 0, &count, axes.data());
    for (unsigned i = 0; i < count; ++i) {
        char tag[5] = {};
        hb_tag_to_string(axes[i].tag, tag);
        out.axes.push_back({tag, axes[i].min_value, axes[i].max_value, axes[i].default_value});
    }
    for (hb_tag_t table : {HB_OT_TAG_GSUB, HB_OT_TAG_GPOS}) {
        for (unsigned start = 0;;) {
            hb_tag_t tags[64];
            unsigned n = 64;
            const unsigned total = hb_ot_layout_table_get_feature_tags(face, table, start, &n, tags);
            for (unsigned i = 0; i < n; ++i) {
                char tag[5] = {};
                hb_tag_to_string(tags[i], tag);
                out.features.emplace_back(tag);
            }
            start += n;
            if (n == 0 || start >= total) break;
        }
    }
    std::sort(out.features.begin(), out.features.end());
    out.features.erase(std::unique(out.features.begin(), out.features.end()), out.features.end());
    return out;
}

} // namespace fonts

const char* registerFontFace(std::string_view id, std::span<const std::uint8_t> bytes, std::string_view sha256) {
    auto& s = state();
    const std::scoped_lock guard(s.lock);
    return s.add(id, bytes, sha256, true);
}

std::string fontFaceSha256(std::string_view id) {
    auto& s = state();
    const std::scoped_lock guard(s.lock);
    const auto found = s.faces.find(id);
    return found == s.faces.end() ? std::string() : found->second->sha256;
}

Rect textGlyphUv(GlyphKey key) {
    auto& s = state();
    const std::scoped_lock guard(s.lock);
    if (key.instance >= s.instances.size()) return {};
    const std::uint64_t cell = std::uint64_t(key.instance) << 32 | key.glyph;
    if (const auto found = s.cells.find(cell); found != s.cells.end()) return found->second;
    auto& instance = *s.instances[key.instance];
    const Rect ink = s.glyph(instance, key.glyph).ink;
    const int width = int(ink.w), height = int(ink.h);
    Outline outline;
    if (width > 0 && height > 0) hb_font_draw_glyph(instance.font, key.glyph, outlineFuncs(), &outline);
    if (outline.vertices.empty()) return s.cells[cell] = Rect{};
    const int cellWidth = align(width + 2 * kTextAtlasPadding), cellHeight = align(height + 2 * kTextAtlasPadding);
    if (cellWidth > kTextAtlasSize || cellHeight > kTextAtlasSize) return {};
    if (s.x + cellWidth > kTextAtlasSize) { s.x = 0; s.y += s.rowHeight; s.rowHeight = 0; }
    if (s.y + cellHeight > kTextAtlasSize) s.clearAtlas();
    std::vector<unsigned char> bitmap(std::size_t(width) * height, 0);
    stbtt__bitmap target{width, height, width, bitmap.data()};
    stbtt_Rasterize(&target, 0.35f, outline.vertices.data(), int(outline.vertices.size()), instance.scale, instance.scale, 0, 0,
                    int(ink.x), int(ink.y), 1, nullptr);
    const int left = s.x + kTextAtlasPadding, top = s.y + kTextAtlasPadding;
    for (int row = 0; row < height; ++row)
        std::copy_n(bitmap.begin() + std::ptrdiff_t(row) * width, width, s.pixels.begin() + std::ptrdiff_t(top + row) * kTextAtlasSize + left);
    s.markDirty(s.x, s.y, s.x + cellWidth, s.y + cellHeight);
    const Rect uv{double(left) / kTextAtlasSize, double(top) / kTextAtlasSize, double(width) / kTextAtlasSize, double(height) / kTextAtlasSize};
    s.x += cellWidth;
    s.rowHeight = std::max(s.rowHeight, cellHeight);
    return s.cells[cell] = uv;
}

std::span<const std::uint8_t> textAtlas() { return state().pixels; }

std::uint64_t textAtlasGeneration() {
    auto& s = state();
    const std::scoped_lock guard(s.lock);
    return s.generation;
}

std::optional<TextAtlasRegion> takeTextAtlasChanges() {
    auto& s = state();
    const std::scoped_lock guard(s.lock);
    if (s.dirtyX0 >= s.dirtyX1 || s.dirtyY0 >= s.dirtyY1) return std::nullopt;
    const TextAtlasRegion region{s.dirtyX0, s.dirtyY0, s.dirtyX1 - s.dirtyX0, s.dirtyY1 - s.dirtyY0};
    s.dirtyX0 = s.dirtyY0 = INT_MAX;
    s.dirtyX1 = s.dirtyY1 = 0;
    return region;
}

} // namespace vc
