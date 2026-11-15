#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "../src/sha256.hpp"
#include "vc/text.hpp"

namespace {

constexpr const char* kInterSha256 = "cf3cb43b0366e2dc6df60e1132b1c9a4c15777f0cd8e5a53e0c15124003e9ed4";

std::vector<std::uint8_t> interBytes() {
    std::ifstream file(VC_FONT_DIR "/Inter.ttf", std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

vc::TextRun run(const char* font, const char* content) {
    vc::TextRun text;
    text.font = font;
    text.content = content;
    return text;
}

} // namespace

TEST_CASE("sha256Hex matches the standard vectors") {
    CHECK(vc::sha256Hex({}) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    const std::vector<std::uint8_t> abc{'a', 'b', 'c'};
    CHECK(vc::sha256Hex(abc) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE("the embedded face reports its hash") {
    CHECK(vc::fontFaceSha256(vc::kEmbeddedInterFace) == kInterSha256);
    CHECK(vc::fontFaceSha256("registry-test/unknown").empty());
}

TEST_CASE("registering a face checks id, hash and bytes") {
    const auto bytes = interBytes();
    REQUIRE(!bytes.empty());
    REQUIRE(vc::sha256Hex(bytes) == kInterSha256);

    SECTION("the real bytes register, again and again") {
        CHECK(vc::registerFontFace("registry-test/inter", bytes, kInterSha256) == nullptr);
        CHECK(vc::registerFontFace("registry-test/inter", bytes, kInterSha256) == nullptr);
        CHECK(vc::fontFaceSha256("registry-test/inter") == kInterSha256);
    }
    SECTION("a wrong hash is refused") {
        const std::string wrong(64, '0');
        CHECK(vc::registerFontFace("registry-test/wrong-hash", bytes, wrong) != nullptr);
        CHECK(vc::fontFaceSha256("registry-test/wrong-hash").empty());
    }
    SECTION("an uppercase or short hash is refused") {
        std::string upper = kInterSha256;
        for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        CHECK(vc::registerFontFace("registry-test/upper", bytes, upper) != nullptr);
        CHECK(vc::registerFontFace("registry-test/short", bytes, std::string(kInterSha256).substr(0, 63)) != nullptr);
    }
    SECTION("an empty id is refused") {
        CHECK(vc::registerFontFace("", bytes, kInterSha256) != nullptr);
    }
    SECTION("the same id with other bytes is refused") {
        REQUIRE(vc::registerFontFace("registry-test/taken", bytes, kInterSha256) == nullptr);
        const std::vector<std::uint8_t> other{'a', 'b', 'c'};
        CHECK(vc::registerFontFace("registry-test/taken", other, vc::sha256Hex(other)) != nullptr);
        CHECK(vc::fontFaceSha256("registry-test/taken") == kInterSha256);
    }
    SECTION("bytes that are not a font are refused") {
        const std::vector<std::uint8_t> garbage(256, 0x5a);
        CHECK(vc::registerFontFace("registry-test/garbage", garbage, vc::sha256Hex(garbage)) != nullptr);
        CHECK(vc::fontFaceSha256("registry-test/garbage").empty());
    }
}

TEST_CASE("the atlas serves glyphs on demand") {
    const auto layout = vc::layoutText(run("inter-regular-v1", "Ag"));
    REQUIRE(layout.glyphs.size() == 2);

    for (const auto& glyph : layout.glyphs) {
        const vc::Rect uv = vc::textGlyphUv(glyph.key);
        CHECK(uv.w > 0.0);
        CHECK(uv.h > 0.0);
        CHECK(uv.x >= 0.0);
        CHECK(uv.y >= 0.0);
        CHECK(uv.x + uv.w <= 1.0);
        CHECK(uv.y + uv.h <= 1.0);
        const vc::Rect again = vc::textGlyphUv(glyph.key);
        CHECK(again.x == uv.x);
        CHECK(again.y == uv.y);
        CHECK(again.w == uv.w);
        CHECK(again.h == uv.h);
    }
}

TEST_CASE("atlas changes are reported once and aligned to the padding") {
    (void)vc::takeTextAtlasChanges();
    const auto layout = vc::layoutText(run("inter-bold-oblique-v1", "Wqj@%"));
    REQUIRE(!layout.glyphs.empty());
    for (const auto& glyph : layout.glyphs) (void)vc::textGlyphUv(glyph.key);

    const auto region = vc::takeTextAtlasChanges();
    REQUIRE(region.has_value());
    CHECK(region->w > 0);
    CHECK(region->h > 0);
    CHECK(region->x % vc::kTextAtlasPadding == 0);
    CHECK(region->y % vc::kTextAtlasPadding == 0);
    CHECK(region->x + region->w <= vc::kTextAtlasSize);
    CHECK(region->y + region->h <= vc::kTextAtlasSize);
    CHECK(!vc::takeTextAtlasChanges().has_value());
}

TEST_CASE("a full atlas clears and serves glyphs again from the top") {
    std::vector<vc::GlyphKey> keys;
    for (const char* font : {"inter-regular-v1", "inter-bold-v1", "inter-oblique-v1", "inter-bold-oblique-v1"}) {
        const auto instance = vc::layoutText(run(font, "A")).glyphs.at(0).key.instance;
        for (std::uint32_t glyph = 1; glyph < 2500; ++glyph) keys.push_back({instance, glyph});
    }
    const auto first = vc::layoutText(run("inter-regular-v1", "A")).glyphs.at(0).key;
    const vc::Rect before = vc::textGlyphUv(first);
    const auto generation = vc::textAtlasGeneration();
    (void)vc::takeTextAtlasChanges();

    std::optional<vc::GlyphKey> trigger;
    for (const auto& key : keys) {
        const vc::Rect uv = vc::textGlyphUv(key);
        if (vc::textAtlasGeneration() == generation) continue;
        trigger = key;
        constexpr double corner = double(vc::kTextAtlasPadding) / vc::kTextAtlasSize;
        CHECK(uv.x == corner);
        CHECK(uv.y == corner);
        break;
    }
    REQUIRE(trigger.has_value());
    CHECK(vc::textAtlasGeneration() == generation + 1);

    const auto region = vc::takeTextAtlasChanges();
    REQUIRE(region.has_value());
    CHECK(region->x == 0);
    CHECK(region->y == 0);
    CHECK(region->w == vc::kTextAtlasSize);
    CHECK(region->h == vc::kTextAtlasSize);

    const auto atlas = vc::textAtlas();
    const auto lowerHalf = atlas.subspan(std::size_t(vc::kTextAtlasSize) * vc::kTextAtlasSize / 2);
    CHECK(std::all_of(lowerHalf.begin(), lowerHalf.end(), [](std::uint8_t p) { return p == 0; }));

    const vc::Rect after = vc::textGlyphUv(first);
    CHECK(after.w == before.w);
    CHECK(after.h == before.h);
    CHECK(after.x > double(vc::kTextAtlasPadding) / vc::kTextAtlasSize);
    const int left = int(after.x * vc::kTextAtlasSize), top = int(after.y * vc::kTextAtlasSize);
    int ink = 0;
    for (int y = top; y < top + int(after.h * vc::kTextAtlasSize); ++y)
        for (int x = left; x < left + int(after.w * vc::kTextAtlasSize); ++x) ink += atlas[std::size_t(y) * vc::kTextAtlasSize + x] > 0;
    CHECK(ink > 0);
}

TEST_CASE("bold and regular glyphs are different instances") {
    const auto regular = vc::layoutText(run("inter-regular-v1", "Ag"));
    const auto bold = vc::layoutText(run("inter-bold-v1", "Ag"));
    REQUIRE(regular.glyphs.size() == bold.glyphs.size());
    REQUIRE(!regular.glyphs.empty());
    CHECK(regular.glyphs[0].key.instance != bold.glyphs[0].key.instance);
}
