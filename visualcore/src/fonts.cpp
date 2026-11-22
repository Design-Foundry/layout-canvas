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
#include <nlohmann/json.hpp>
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
const unsigned char kSerif[] = {
#include "vc_serif_font.inc"
};
const unsigned char kMono[] = {
#include "vc_mono_font.inc"
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
    std::unordered_map<std::string, Rect> named;
    int x = 0, y = 0, rowHeight = 0;
    std::uint64_t generation = 0;
    std::map<std::string, std::vector<FontFile>, std::less<>> families;
    std::uint64_t registrations = 0;
    int dirtyX0 = INT_MAX, dirtyY0 = INT_MAX, dirtyX1 = 0, dirtyY1 = 0;

    State() {
        if (add(kEmbeddedInterFace, {kInter, sizeof kInter}, VC_INTER_SHA256, false)) std::abort();
        if (add(fonts::kSerifFace, {kSerif, sizeof kSerif}, VC_SERIF_SHA256, false)) std::abort();
        if (add(fonts::kMonoFace, {kMono, sizeof kMono}, VC_MONO_SHA256, false)) std::abort();
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
        ++registrations;
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
        named.clear();
        x = y = rowHeight = 0;
        ++generation;
        markDirty(0, 0, kTextAtlasSize, kTextAtlasSize);
    }
};

State& state() {
    static State instance;
    return instance;
}

bool fitsAtlas(int width, int height) {
    return align(width + 2 * kTextAtlasPadding) <= kTextAtlasSize && align(height + 2 * kTextAtlasPadding) <= kTextAtlasSize;
}

/// Copies a bitmap into the next free cell, clearing the atlas when it is full. The caller holds the lock.
Rect placeInAtlas(State& s, int width, int height, const unsigned char* bitmap) {
    const int cellWidth = align(width + 2 * kTextAtlasPadding), cellHeight = align(height + 2 * kTextAtlasPadding);
    if (s.x + cellWidth > kTextAtlasSize) { s.x = 0; s.y += s.rowHeight; s.rowHeight = 0; }
    if (s.y + cellHeight > kTextAtlasSize) s.clearAtlas();
    const int left = s.x + kTextAtlasPadding, top = s.y + kTextAtlasPadding;
    for (int row = 0; row < height; ++row)
        std::copy_n(bitmap + std::ptrdiff_t(row) * width, width, s.pixels.begin() + std::ptrdiff_t(top + row) * kTextAtlasSize + left);
    s.markDirty(s.x, s.y, s.x + cellWidth, s.y + cellHeight);
    const Rect uv{double(left) / kTextAtlasSize, double(top) / kTextAtlasSize, double(width) / kTextAtlasSize, double(height) / kTextAtlasSize};
    s.x += cellWidth;
    s.rowHeight = std::max(s.rowHeight, cellHeight);
    return uv;
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
    auto& s = state();
    const std::scoped_lock guard(s.lock);
    const auto face = s.faces.find(faceId);
    if (face == s.faces.end()) return -1;
    std::vector<Variation> sorted;
    for (const auto& v : variations) {
        hb_ot_var_axis_info_t info{};
        if (hb_ot_var_find_axis_info(face->second->face, v.tag, &info) && info.default_value == v.value) continue;
        sorted.push_back(v);
    }
    std::sort(sorted.begin(), sorted.end(), [](const Variation& a, const Variation& b) { return a.tag < b.tag; });
    std::string key(faceId);
    for (const auto& v : sorted) {
        char part[48];
        std::snprintf(part, sizeof part, "|%08x=%a", unsigned(v.tag), double(v.value));
        key += part;
    }
    if (const auto found = s.instanceIds.find(key); found != s.instanceIds.end()) return int(found->second);
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

bool attachesToPrevious(std::uint32_t codepoint) {
    if (codepoint == 0x200d || (codepoint >= 0xfe00 && codepoint <= 0xfe0f) || (codepoint >= 0xe0100 && codepoint <= 0xe01ef)) return true;
    const auto category = hb_unicode_general_category(hb_unicode_funcs_get_default(), codepoint);
    return category == HB_UNICODE_GENERAL_CATEGORY_NON_SPACING_MARK || category == HB_UNICODE_GENERAL_CATEGORY_SPACING_MARK ||
           category == HB_UNICODE_GENERAL_CATEGORY_ENCLOSING_MARK;
}

ShapedGlyph missingGlyph(std::uint32_t instanceIndex) {
    auto& s = state();
    const std::scoped_lock guard(s.lock);
    auto& instance = *s.instances.at(instanceIndex);
    const auto& metrics = s.glyph(instance, 0);
    return {0, 0, double(float(metrics.advance) * instance.scale), 0, 0, 0, metrics.ink, instanceIndex};
}

bool covers(std::uint32_t instance, std::uint32_t codepoint) {
    auto& s = state();
    hb_font_t* font = nullptr;
    {
        const std::scoped_lock guard(s.lock);
        font = s.instances.at(instance)->font;
    }
    hb_codepoint_t glyph = 0;
    return hb_font_get_nominal_glyph(font, codepoint, &glyph);
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
                  double(float(positions[i].x_offset) * scale), double(float(positions[i].y_offset) * scale), metrics.ink, instanceIndex};
    }
    hb_buffer_destroy(buffer);
    if (s.shaped.size() >= kShapeCacheLimit) s.shaped.clear();
    s.shaped.emplace(std::move(key), out);
    return out;
}

Capabilities capabilitiesOf(hb_face_t* face) {
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

std::optional<Capabilities> capabilities(std::string_view faceId) {
    auto& s = state();
    const std::scoped_lock guard(s.lock);
    const auto found = s.faces.find(faceId);
    if (found == s.faces.end()) return std::nullopt;
    return capabilitiesOf(found->second->face);
}

} // namespace fonts

const char* registerFontFace(std::string_view id, std::span<const std::uint8_t> bytes, std::string_view sha256) {
    auto& s = state();
    const std::scoped_lock guard(s.lock);
    return s.add(id, bytes, sha256, true);
}

namespace {
std::string familyKey(std::string_view provider, std::string_view key, std::string_view version) {
    std::string out(provider);
    out += '\x1f';
    out += key;
    out += '\x1f';
    out += version;
    return out;
}
} // namespace

namespace fonts {
std::optional<std::vector<FontFile>> familyFiles(std::string_view provider, std::string_view key, std::string_view version) {
    auto& s = state();
    const std::scoped_lock guard(s.lock);
    const auto found = s.families.find(familyKey(provider, key, version));
    if (found == s.families.end()) return std::nullopt;
    return found->second;
}
} // namespace fonts

const char* registerFontFamily(std::string_view provider, std::string_view key, std::string_view version, std::vector<FontFile> files) {
    if (provider.empty() || key.empty()) return "provider and key are required";
    if (files.empty() || files.size() > 64) return "a family has 1 to 64 files";
    for (const auto& file : files) {
        if (file.face.empty() || file.face.size() > 512) return "a file's face id must have 1 to 512 characters";
        if (const char* why = fontSettingsProblem(file.fixed, {})) return why;
    }
    auto& s = state();
    const std::scoped_lock guard(s.lock);
    auto [entry, inserted] = s.families.try_emplace(familyKey(provider, key, version), files);
    if (!inserted) {
        if (entry->second == files) return nullptr;
        return "this family and version already has different files";
    }
    ++s.registrations;
    return nullptr;
}

std::uint64_t fontRegistryGeneration() {
    auto& s = state();
    const std::scoped_lock guard(s.lock);
    return s.registrations;
}

std::optional<std::string> fontCapabilitiesJson(std::span<const std::uint8_t> bytes) {
    if (bytes.empty() || bytes.size() > kMaxFaceBytes) return std::nullopt;
    hb_blob_t* blob = hb_blob_create(reinterpret_cast<const char*>(bytes.data()), unsigned(bytes.size()), HB_MEMORY_MODE_READONLY, nullptr, nullptr);
    hb_face_t* face = hb_face_create(blob, 0);
    std::optional<std::string> out;
    const auto upem = hb_face_get_upem(face);
    if (hb_face_get_glyph_count(face) > 0 && upem >= 16 && upem <= 16384) {
        const auto read = fonts::capabilitiesOf(face);
        nlohmann::json axes = nlohmann::json::array();
        for (const auto& axis : read.axes) axes.push_back({{"tag", axis.tag}, {"min", axis.min}, {"max", axis.max}, {"default", axis.defaultValue}});
        out = nlohmann::json{{"axes", axes}, {"features", read.features}}.dump();
    }
    hb_face_destroy(face);
    hb_blob_destroy(blob);
    return out;
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
    if (!fitsAtlas(width, height)) return {};
    std::vector<unsigned char> bitmap(std::size_t(width) * height, 0);
    stbtt__bitmap target{width, height, width, bitmap.data()};
    stbtt_Rasterize(&target, 0.35f, outline.vertices.data(), int(outline.vertices.size()), instance.scale, instance.scale, 0, 0,
                    int(ink.x), int(ink.y), 1, nullptr);
    return s.cells[cell] = placeInAtlas(s, width, height, bitmap.data());
}

namespace fonts {
std::optional<Rect> atlasFind(const std::string& name) {
    auto& s = state();
    const std::scoped_lock guard(s.lock);
    const auto found = s.named.find(name);
    if (found == s.named.end()) return std::nullopt;
    return found->second;
}

void atlasNote(const std::string& name) {
    auto& s = state();
    const std::scoped_lock guard(s.lock);
    s.named[name] = Rect{};
}

Rect atlasPlace(const std::string& name, int width, int height, std::span<const std::uint8_t> bitmap) {
    auto& s = state();
    const std::scoped_lock guard(s.lock);
    if (width <= 0 || height <= 0 || bitmap.size() < std::size_t(width) * std::size_t(height) || !fitsAtlas(width, height)) return {};
    return s.named[name] = placeInAtlas(s, width, height, bitmap.data());
}
} // namespace fonts

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
