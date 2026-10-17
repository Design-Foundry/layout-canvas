#pragma once
#include <string>
#include <variant>

namespace vc {

/// Reference to a design token by its name in the pinned release.
struct TokenRef {
    std::string name;

    friend bool operator==(const TokenRef&, const TokenRef&) = default;
};

/// A styleable property: either a raw value or a token reference.
template <typename T>
using Value = std::variant<T, TokenRef>;

} // namespace vc
