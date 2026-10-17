#pragma once
#include <optional>
#include <string>
#include <string_view>

namespace vc {

/// Returns a key that sorts strictly between `before` and `after`.
/// An empty `before` means "start"; no `after` means "end".
[[nodiscard]] std::string keyBetween(std::string_view before, std::optional<std::string_view> after);

} // namespace vc
