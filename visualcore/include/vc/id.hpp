#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <random>
#include <string>
#include <string_view>

namespace vc {

/// 128-bit identifier for nodes and ops. Written as a UUID on the wire.
struct Id {
    std::uint64_t hi = 0;
    std::uint64_t lo = 0;

    [[nodiscard]] bool isNil() const { return hi == 0 && lo == 0; }
    [[nodiscard]] std::string toString() const;
    [[nodiscard]] static std::optional<Id> parse(std::string_view text);

    friend bool operator==(const Id&, const Id&) = default;
    friend auto operator<=>(const Id&, const Id&) = default;
};

/// Produces random (version 4) ids.
class IdGenerator {
public:
    IdGenerator();
    explicit IdGenerator(std::uint64_t seed);

    [[nodiscard]] Id next();

private:
    std::mt19937_64 rng_;
};

} // namespace vc

template <>
struct std::hash<vc::Id> {
    std::size_t operator()(const vc::Id& id) const noexcept {
        return std::hash<std::uint64_t>{}(id.hi ^ (id.lo * 0x9E3779B97F4A7C15ull));
    }
};
