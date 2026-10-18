#pragma once
#include <variant>
#include "vc/id.hpp"

namespace vc {

/// Reference to a design token by id, so renaming the token never breaks it.
struct TokenRef {
    Id token;

    friend bool operator==(const TokenRef&, const TokenRef&) = default;
};

/// A styleable property: either a raw value or a token reference.
template <typename T>
using Value = std::variant<T, TokenRef>;

} // namespace vc
