#include "vc/id.hpp"

namespace vc {
namespace {

constexpr char kHex[] = "0123456789abcdef";

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool isDashPosition(std::size_t i) {
    return i == 8 || i == 13 || i == 18 || i == 23;
}

} // namespace

std::string Id::toString() const {
    std::string out;
    out.reserve(36);
    for (int nibble = 0; nibble < 32; ++nibble) {
        if (nibble == 8 || nibble == 12 || nibble == 16 || nibble == 20) {
            out.push_back('-');
        }
        const std::uint64_t word = nibble < 16 ? hi : lo;
        const int shift = 60 - (nibble % 16) * 4;
        out.push_back(kHex[(word >> shift) & 0xF]);
    }
    return out;
}

std::optional<Id> Id::parse(std::string_view text) {
    if (text.size() != 36) {
        return std::nullopt;
    }
    Id id;
    int nibble = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (isDashPosition(i)) {
            if (text[i] != '-') return std::nullopt;
            continue;
        }
        const int value = hexValue(text[i]);
        if (value < 0) return std::nullopt;
        std::uint64_t& word = nibble < 16 ? id.hi : id.lo;
        word = (word << 4) | static_cast<std::uint64_t>(value);
        ++nibble;
    }
    return id;
}

IdGenerator::IdGenerator() {
    std::random_device device;
    rng_.seed((std::uint64_t{device()} << 32) | device());
}

IdGenerator::IdGenerator(std::uint64_t seed) : rng_{seed} {}

Id IdGenerator::next() {
    Id id{rng_(), rng_()};
    id.hi = (id.hi & ~0xF000ull) | 0x4000ull;
    id.lo = (id.lo & ~(0xC000ull << 48)) | (0x8000ull << 48);
    return id;
}

} // namespace vc
