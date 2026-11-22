#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include "vc/id.hpp"
#include "vc/math.hpp"

namespace vc {

/// A named choice ("rounded") or a number (500) on one axis of an icon set.
using IconAxisValue = std::variant<std::string, double>;
using IconAxes = std::map<std::string, IconAxisValue>;

/// One discrete axis of an icon set: every value the source has an SVG for.
struct IconAxis {
    std::string tag;
    std::vector<IconAxisValue> values;
    IconAxisValue defaultValue;
    friend bool operator==(const IconAxis&, const IconAxis&) = default;
};

/// A set of icons the document may use. The shape of a Design Foundry icon resource.
struct IconSet {
    Id id;
    /// The first segment of the path form of a reference, such as `material-symbols`.
    std::string identifier;
    std::string displayName;
    /// The source commit the set is pinned to.
    std::string version;
    std::vector<IconAxis> axes;
    [[nodiscard]] const IconAxis* axis(std::string_view tag) const;
    friend bool operator==(const IconSet&, const IconSet&) = default;
};

/// The Material Symbols set and its fixed id. Every document knows it and cannot change it.
inline constexpr Id kMaterialSymbolsSetId{0, 0x200};
[[nodiscard]] const IconSet& materialSymbolsSet();
/// The sets every document has.
[[nodiscard]] const std::vector<IconSet>& builtinIconSets();
[[nodiscard]] const IconSet* findIconSet(const Id& id);
[[nodiscard]] const IconSet* findIconSetByIdentifier(std::string_view identifier);

/// What an icon node draws: a glyph of a set at axis values. Axes at their default are not stored.
struct IconRef {
    Id set;
    std::string glyph;
    IconAxes axes;
    friend bool operator==(const IconRef&, const IconRef&) = default;
};

/// Default box of a new icon.
inline constexpr double kDefaultIconSize = 24.0;

/// Lowercase letters, digits and underscores, up to 64 characters.
[[nodiscard]] bool validIconGlyph(std::string_view glyph);
/// Why `ref` is malformed, or null. A glyph the source lacks is not a problem.
[[nodiscard]] const char* iconProblem(const IconRef& ref);
/// Drops axes at their default and checks the rest. Returns why it refuses on failure.
[[nodiscard]] const char* normalizeIcon(IconRef& ref);
/// The path form, `material-symbols/arrow_back?style=rounded&FILL=1`, listing only non-default axes.
[[nodiscard]] std::string iconPath(const IconRef& ref);
/// Reads the path form, `<set>/<glyph>` with optional `?axis=value&...` (axis names are case-insensitive).
/// Null and `problem` set when it is not one.
[[nodiscard]] std::optional<IconRef> parseIconPath(std::string_view text, const char** problem = nullptr);
/// The axis values of `ref` with defaults filled in.
[[nodiscard]] IconAxes resolvedIconAxes(const IconRef& ref);
/// The registry key of the SVG for `ref`, with every axis explicit:
/// `material-symbols/arrow_back?style=outlined&FILL=0&wght=400&opsz=24`.
[[nodiscard]] std::string iconSourceKey(const IconRef& ref);

inline constexpr std::size_t kMaxIconSvgBytes = 64 * 1024;
inline constexpr int kMaxIconRasterSide = 512;

/// Why `svg` falls outside the accepted subset (path, basic shapes, groups, transforms, fills and
/// strokes, within size bounds), or null.
[[nodiscard]] const char* iconSvgProblem(std::string_view svg);

/// Coverage of an SVG inside a `side` by `side` square, viewBox fitted and centered.
struct IconMask {
    int side = 0;
    std::vector<std::uint8_t> coverage;
};
[[nodiscard]] std::optional<IconMask> rasterizeIconSvg(std::string_view svg, int side);

/// Registers the SVG of `key`. Returns null, or why it refused. Registering the same key with the same
/// bytes again succeeds; different bytes are refused.
[[nodiscard]] const char* registerIconSvg(std::string_view key, std::string_view svg);
[[nodiscard]] bool iconRegistered(std::string_view key);
/// Grows whenever an icon is registered.
[[nodiscard]] std::uint64_t iconRegistryGeneration();
/// Normalized atlas rectangle of icon `key` rasterized in a `side` pixel square, shared with the text
/// glyph atlas. Empty when the icon is not registered or has no ink.
[[nodiscard]] Rect iconGlyphUv(std::string_view key, int side);
/// The power-of-two raster side that keeps an icon drawn at `drawnPixels` sharp.
[[nodiscard]] int iconRasterSide(double drawnPixels);

} // namespace vc
