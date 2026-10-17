#include "vc/fractional_index.hpp"

#include <cassert>

namespace vc {
namespace {

// Keys are an integer part followed by a fraction. The integer's first character encodes its length
// ('a' = 1 digit, 'b' = 2, ... and 'Z' = 1 digit, 'Y' = 2, ... for negatives), so appending at either
// end grows keys logarithmically instead of linearly.

constexpr std::string_view kDigits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
constexpr int kBase = static_cast<int>(kDigits.size());
constexpr std::string_view kZero = "a0";

int digitOf(char c) {
    return static_cast<int>(kDigits.find(c));
}

char digitAt(std::string_view s, std::size_t i) {
    return i < s.size() ? s[i] : '0';
}

std::size_t integerLength(char head) {
    if (head >= 'a' && head <= 'z') return static_cast<std::size_t>(head - 'a') + 2;
    return static_cast<std::size_t>('Z' - head) + 2;
}

std::string_view integerPart(std::string_view key) {
    return key.substr(0, integerLength(key[0]));
}

std::string midpoint(std::string_view before, std::optional<std::string_view> after) {
    std::string prefix;
    while (true) {
        if (after) {
            std::size_t n = 0;
            while (digitAt(before, n) == digitAt(*after, n)) {
                ++n;
            }
            if (n > 0) {
                prefix.append(after->substr(0, n));
                before = before.size() > n ? before.substr(n) : std::string_view{};
                after = after->substr(n);
                continue;
            }
        }

        const int low = before.empty() ? 0 : digitOf(before[0]);
        const int high = after ? digitOf((*after)[0]) : kBase;
        if (high - low > 1) {
            prefix.push_back(kDigits[static_cast<std::size_t>((low + high + 1) / 2)]);
            return prefix;
        }
        if (after && after->size() > 1) {
            prefix.push_back((*after)[0]);
            return prefix;
        }
        prefix.push_back(kDigits[static_cast<std::size_t>(low)]);
        before = before.size() > 1 ? before.substr(1) : std::string_view{};
        after = std::nullopt;
    }
}

std::optional<std::string> increment(std::string_view integer) {
    std::string digits{integer.substr(1)};
    for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
        const int d = digitOf(*it) + 1;
        if (d < kBase) {
            *it = kDigits[static_cast<std::size_t>(d)];
            return integer[0] + digits;
        }
        *it = '0';
    }
    const char head = integer[0];
    if (head == 'Z') return std::string{kZero};
    if (head == 'z') return std::nullopt;
    const char next = static_cast<char>(head + 1);
    if (next > 'a') {
        digits.push_back('0');
    } else {
        digits.pop_back();
    }
    return next + digits;
}

std::optional<std::string> decrement(std::string_view integer) {
    std::string digits{integer.substr(1)};
    for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
        const int d = digitOf(*it) - 1;
        if (d >= 0) {
            *it = kDigits[static_cast<std::size_t>(d)];
            return integer[0] + digits;
        }
        *it = kDigits.back();
    }
    const char head = integer[0];
    if (head == 'a') return std::string{"Z"} + kDigits.back();
    if (head == 'A') return std::nullopt;
    const char next = static_cast<char>(head - 1);
    if (next < 'Z') {
        digits.push_back(kDigits.back());
    } else {
        digits.pop_back();
    }
    return next + digits;
}

} // namespace

std::string keyBetween(std::string_view before, std::optional<std::string_view> after) {
    assert(!after || before < *after);

    if (before.empty() && !after) {
        return std::string{kZero};
    }
    if (before.empty()) {
        const std::string_view integer = integerPart(*after);
        if (integer.size() < after->size()) {
            return std::string{integer};
        }
        if (auto lower = decrement(integer)) {
            return *lower;
        }
        return std::string{integer} + midpoint("", after->substr(integer.size()));
    }
    const std::string_view integer = integerPart(before);
    const std::string_view fraction = before.substr(integer.size());
    if (!after) {
        if (auto higher = increment(integer)) {
            return *higher;
        }
        return std::string{integer} + midpoint(fraction, std::nullopt);
    }
    if (integer == integerPart(*after)) {
        return std::string{integer} + midpoint(fraction, after->substr(integer.size()));
    }
    auto higher = increment(integer);
    if (higher && *higher < *after) {
        return *higher;
    }
    return std::string{integer} + midpoint(fraction, std::nullopt);
}

} // namespace vc
