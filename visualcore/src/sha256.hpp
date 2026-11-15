#pragma once
#include <cstdint>
#include <span>
#include <string>

namespace vc {

/// Lowercase hexadecimal SHA-256 of `bytes`.
[[nodiscard]] std::string sha256Hex(std::span<const std::uint8_t> bytes);

} // namespace vc
