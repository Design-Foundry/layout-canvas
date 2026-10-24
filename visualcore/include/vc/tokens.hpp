#pragma once
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include "vc/color.hpp"
#include "vc/id.hpp"
#include "vc/node.hpp"

namespace vc {

/// Token families, after Design Foundry's model 0.4. Members of a family take the same operations.
/// Typography is a literal composite of the editor's supported text styles.
enum class TokenType { Color, Dimension, Duration, Ratio, Opacity, Border, Shadow, Typography };

enum class Unit { None, Px, Rem, Em, Pt, Ms, S };

/// A number with its unit. Ratio and opacity use `Unit::None`.
struct Magnitude {
    double value = 0.0;
    Unit unit = Unit::None;

    friend bool operator==(const Magnitude&, const Magnitude&) = default;
};

enum class ColorChannel { L, C, H, Alpha };
enum class ColorOp { Set, Delta, Factor };

/// One channel operation. The operand is `value`, or for alpha an opacity token in `ref`.
struct ColorChannelModifier {
    ColorChannel channel = ColorChannel::L;
    ColorOp op = ColorOp::Set;
    std::optional<double> value;
    std::optional<Id> ref;

    friend bool operator==(const ColorChannelModifier&, const ColorChannelModifier&) = default;
};

/// Mixes the color with another color token, `amount` of the way towards it (0 to 1).
struct ColorMixModifier {
    Id token;
    double amount = 0.5;

    friend bool operator==(const ColorMixModifier&, const ColorMixModifier&) = default;
};

enum class MagnitudeOp { Factor, Delta, Min, Max };

/// One step for dimension, duration, ratio and opacity tokens. The operand is `value`, or a token
/// of the same family in `ref`.
struct MagnitudeModifier {
    MagnitudeOp op = MagnitudeOp::Factor;
    std::optional<double> value;
    std::optional<Id> ref;

    friend bool operator==(const MagnitudeModifier&, const MagnitudeModifier&) = default;
};

using Modifier = std::variant<ColorChannelModifier, ColorMixModifier, MagnitudeModifier>;

/// A border token: a dimension token for the width, a line style, a color token.
struct BorderValue {
    Id width;
    StrokeStyle style = StrokeStyle::Solid;
    Id color;

    friend bool operator==(const BorderValue&, const BorderValue&) = default;
};

/// One layer of a shadow token. Offsets are literal; the color is a token, optionally modified.
struct ShadowLayerValue {
    double x = 0.0;
    double y = 0.0;
    double blur = 0.0;
    double spread = 0.0;
    bool inset = false;
    Id color;
    std::vector<Modifier> colorModifiers;

    friend bool operator==(const ShadowLayerValue&, const ShadowLayerValue&) = default;
};

struct ShadowValue {
    std::vector<ShadowLayerValue> layers;

    friend bool operator==(const ShadowValue&, const ShadowValue&) = default;
};

/// A token's own value. Colors are born solid: alpha comes from modifiers.
using TokenLiteral = std::variant<Oklch, Magnitude, BorderValue, ShadowValue, TextStyle>;

/// A design token. It is a primitive (`value`, the same in every mode) or an alias (`source`, one
/// token of the same family per mode, covering the default mode). Modifiers apply after either.
/// Border, shadow and typography tokens are composites: primitives only, without modifiers.
struct Token {
    Id id;
    TokenType type = TokenType::Color;
    /// Renameable handle, unique within the family: kebab-case segments, optionally dotted.
    std::string identifier;
    std::string displayName;
    std::string description;
    bool deprecated = false;
    /// Fractional index among tokens of the same family, for display order.
    std::string order;

    std::optional<TokenLiteral> value;
    std::map<Id, Id> source;
    std::vector<Modifier> modifiers;

    [[nodiscard]] bool isAlias() const { return !source.empty(); }
    friend bool operator==(const Token&, const Token&) = default;
};

struct Mode {
    Id id;
    std::string name;
    std::string order;

    friend bool operator==(const Mode&, const Mode&) = default;
};

/// Calls `fn` with every token id `token` depends on: alias sources and modifier operands.
template <typename Fn>
void forEachTokenReference(const Token& token, Fn&& fn) {
    for (const auto& [mode, source] : token.source) {
        fn(source);
    }
    const auto visitModifiers = [&](const std::vector<Modifier>& modifiers) {
        for (const Modifier& modifier : modifiers) {
            if (const auto* channel = std::get_if<ColorChannelModifier>(&modifier); channel && channel->ref) {
                fn(*channel->ref);
            } else if (const auto* mixer = std::get_if<ColorMixModifier>(&modifier)) {
                fn(mixer->token);
            } else if (const auto* step = std::get_if<MagnitudeModifier>(&modifier); step && step->ref) {
                fn(*step->ref);
            }
        }
    };
    visitModifiers(token.modifiers);
    if (token.value) {
        if (const auto* border = std::get_if<BorderValue>(&*token.value)) {
            fn(border->width);
            fn(border->color);
        } else if (const auto* shadow = std::get_if<ShadowValue>(&*token.value)) {
            for (const ShadowLayerValue& layer : shadow->layers) {
                fn(layer.color);
                visitModifiers(layer.colorModifiers);
            }
        }
    }
}

/// The mode every document has and can't delete. Aliases must name a source for it.
inline constexpr Id kDefaultModeId{0, 1};

[[nodiscard]] bool isValidIdentifier(std::string_view identifier);
[[nodiscard]] std::string_view tokenTypeName(TokenType type);
[[nodiscard]] std::optional<TokenType> tokenTypeFromName(std::string_view name);
[[nodiscard]] std::string_view unitName(Unit unit);
[[nodiscard]] std::optional<Unit> unitFromName(std::string_view name);
/// The units a family accepts. The first one is the default for new tokens.
[[nodiscard]] std::vector<Unit> unitsFor(TokenType type);

} // namespace vc
