#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <algorithm>
#include <optional>
#include <string>
#include <vector>
#include "vc/color.hpp"
#include "vc/fractional_index.hpp"
#include "vc/id.hpp"

using Catch::Matchers::WithinAbs;

TEST_CASE("ids round-trip through their uuid text") {
    vc::IdGenerator ids{42};
    const vc::Id id = ids.next();
    const std::string text = id.toString();

    REQUIRE(text.size() == 36);
    REQUIRE(text[14] == '4');
    REQUIRE(vc::Id::parse(text) == id);
}

TEST_CASE("id parsing rejects malformed text") {
    REQUIRE_FALSE(vc::Id::parse("not-a-uuid"));
    REQUIRE_FALSE(vc::Id::parse("0123456789abcdef0123456789abcdef0123"));
    REQUIRE_FALSE(vc::Id::parse("0123456z-89ab-cdef-0123-456789abcdef"));
    REQUIRE(vc::Id::parse("0123456789AB-cdef-0123-456789abcdef") == std::nullopt);
    REQUIRE(vc::Id::parse("01234567-89AB-cdef-0123-456789abcdef")->toString() ==
            "01234567-89ab-cdef-0123-456789abcdef");
}

TEST_CASE("generated ids differ") {
    vc::IdGenerator ids{1};
    REQUIRE(ids.next() != ids.next());
}

TEST_CASE("oklch converts to the expected srgb") {
    const vc::Color white = vc::toSrgb({1.0, 0.0, 0.0});
    REQUIRE_THAT(white.r, WithinAbs(1.0, 1e-4));
    REQUIRE_THAT(white.g, WithinAbs(1.0, 1e-4));
    REQUIRE_THAT(white.b, WithinAbs(1.0, 1e-4));

    const vc::Color red = vc::toSrgb({0.627955, 0.257683, 29.2339, 0.5});
    REQUIRE_THAT(red.r, WithinAbs(1.0, 1e-3));
    REQUIRE_THAT(red.g, WithinAbs(0.0, 1e-3));
    REQUIRE_THAT(red.b, WithinAbs(0.0, 1e-3));
    REQUIRE_THAT(red.a, WithinAbs(0.5, 1e-6));
}

TEST_CASE("out-of-gamut oklch is clipped") {
    const vc::Color c = vc::toSrgb({0.9, 0.4, 140.0});
    for (float channel : {c.r, c.g, c.b}) {
        REQUIRE(channel >= 0.0f);
        REQUIRE(channel <= 1.0f);
    }
}

TEST_CASE("keys sort between their neighbours") {
    const std::string first = vc::keyBetween("", std::nullopt);
    const std::string after = vc::keyBetween(first, std::nullopt);
    const std::string before = vc::keyBetween("", first);
    const std::string middle = vc::keyBetween(first, after);

    REQUIRE(before < first);
    REQUIRE(first < middle);
    REQUIRE(middle < after);
}

TEST_CASE("repeated inserts at the same spot keep strict order") {
    std::vector<std::string> keys{vc::keyBetween("", std::nullopt)};
    for (int i = 0; i < 200; ++i) {
        keys.insert(keys.begin(), vc::keyBetween("", keys.front()));
        keys.push_back(vc::keyBetween(keys.back(), std::nullopt));
        keys.insert(keys.begin() + 1, vc::keyBetween(keys[0], keys[1]));
    }

    REQUIRE(std::is_sorted(keys.begin(), keys.end()));
    REQUIRE(std::adjacent_find(keys.begin(), keys.end()) == keys.end());
}

TEST_CASE("appending or prepending many times keeps keys short") {
    std::string last;
    std::string first = vc::keyBetween("", std::nullopt);
    for (int i = 0; i < 10000; ++i) {
        const std::string next = vc::keyBetween(last, std::nullopt);
        REQUIRE(last < next);
        last = next;
        const std::string prev = vc::keyBetween("", first);
        REQUIRE(prev < first);
        first = prev;
    }
    REQUIRE(last.size() <= 4);
    REQUIRE(first.size() <= 4);
}
