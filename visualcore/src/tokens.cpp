#include "vc/tokens.hpp"

#include <array>
#include <utility>

namespace vc {
namespace {

constexpr std::array<std::pair<TokenType, std::string_view>, 5> kTypeNames{{
    {TokenType::Color, "color"},
    {TokenType::Dimension, "dimension"},
    {TokenType::Duration, "duration"},
    {TokenType::Ratio, "ratio"},
    {TokenType::Opacity, "opacity"},
}};

constexpr std::array<std::pair<Unit, std::string_view>, 7> kUnitNames{{
    {Unit::None, ""},
    {Unit::Px, "px"},
    {Unit::Rem, "rem"},
    {Unit::Em, "em"},
    {Unit::Pt, "pt"},
    {Unit::Ms, "ms"},
    {Unit::S, "s"},
}};

bool isLower(char c) {
    return c >= 'a' && c <= 'z';
}

bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

} // namespace

bool isValidIdentifier(std::string_view identifier) {
    // ^[a-z][a-z0-9]*(-[a-z0-9]+)*(\.[a-z][a-z0-9]*(-[a-z0-9]+)*)*$
    std::size_t i = 0;
    while (true) {
        if (i >= identifier.size() || !isLower(identifier[i])) {
            return false;
        }
        ++i;
        while (i < identifier.size() && (isLower(identifier[i]) || isDigit(identifier[i]))) {
            ++i;
        }
        while (i < identifier.size() && identifier[i] == '-') {
            ++i;
            const std::size_t start = i;
            while (i < identifier.size() && (isLower(identifier[i]) || isDigit(identifier[i]))) {
                ++i;
            }
            if (i == start) {
                return false;
            }
        }
        if (i == identifier.size()) {
            return true;
        }
        if (identifier[i] != '.') {
            return false;
        }
        ++i;
    }
}

std::string_view tokenTypeName(TokenType type) {
    for (const auto& [value, name] : kTypeNames) {
        if (value == type) return name;
    }
    return "color";
}

std::optional<TokenType> tokenTypeFromName(std::string_view name) {
    for (const auto& [value, text] : kTypeNames) {
        if (text == name) return value;
    }
    return std::nullopt;
}

std::string_view unitName(Unit unit) {
    for (const auto& [value, name] : kUnitNames) {
        if (value == unit) return name;
    }
    return "";
}

std::optional<Unit> unitFromName(std::string_view name) {
    for (const auto& [value, text] : kUnitNames) {
        if (text == name) return value;
    }
    return std::nullopt;
}

std::vector<Unit> unitsFor(TokenType type) {
    switch (type) {
    case TokenType::Dimension:
        return {Unit::Px, Unit::Rem, Unit::Em, Unit::Pt};
    case TokenType::Duration:
        return {Unit::Ms, Unit::S};
    case TokenType::Ratio:
    case TokenType::Opacity:
        return {Unit::None};
    case TokenType::Color:
        break;
    }
    return {};
}

} // namespace vc
