#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <set>
#include <string>
#include "../src/fonts.hpp"
#include "vc/text.hpp"

using namespace vc;
using Catch::Matchers::WithinAbs;

namespace {

std::string utf8(std::uint32_t cp) {
    std::string out;
    if (cp < 0x80) out += char(cp);
    else if (cp < 0x800) { out += char(0xc0 | (cp >> 6)); out += char(0x80 | (cp & 0x3f)); }
    else if (cp < 0x10000) { out += char(0xe0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3f)); out += char(0x80 | (cp & 0x3f)); }
    else { out += char(0xf0 | (cp >> 18)); out += char(0x80 | ((cp >> 12) & 0x3f)); out += char(0x80 | ((cp >> 6) & 0x3f)); out += char(0x80 | (cp & 0x3f)); }
    return out;
}

std::uint32_t instanceOf(const Font& font) { return std::uint32_t(fonts::instanceIndex(font.face(), {})); }

/// A letter the first font lacks and the second covers.
std::uint32_t onlyIn(const Font& missing, const Font& present) {
    for (std::uint32_t cp = 0x21; cp < 0x3000; ++cp)
        if (!fonts::attachesToPrevious(cp) && !fonts::covers(instanceOf(missing), cp) && fonts::covers(instanceOf(present), cp)) return cp;
    FAIL("no such character");
    return 0;
}

TextRun run(std::string content) {
    TextRun text;
    text.content = std::move(content);
    text.fontSize = 32;
    return text;
}

std::set<std::uint32_t> instances(const TextLayout& layout) {
    std::set<std::uint32_t> out;
    for (const auto& glyph : layout.glyphs) out.insert(glyph.key.instance);
    return out;
}

Font unregistered() {
    Font font = embeddedInterFont();
    font.provider = "test";
    font.key = "not-registered";
    return font;
}

} // namespace

TEST_CASE("characters outside Latin are accepted and drawn") {
    for (const char* content : {"\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82", "\xce\x93\xce\xb5\xce\xb9\xce\xac", "\xd8\xa7\xd9\x84",
                                "\xe4\xb8\xad", "\xf0\x9f\x98\x80", "a\xcc\x81"}) {
        const auto text = run(content);
        REQUIRE(validText(text));
        CHECK_FALSE(layoutText(text).glyphs.empty());
    }
}

TEST_CASE("a character no font covers draws the missing-glyph mark of the first font") {
    TextRun text = run("\xe4\xb8\xad");
    const auto layout = layoutText(text, TextFonts{{&embeddedInterFont()}, GenericFallback::None});
    REQUIRE(layout.glyphs.size() == 1);
    CHECK(layout.glyphs[0].key.glyph == 0);
    CHECK(layout.glyphs[0].key.instance == instanceOf(embeddedInterFont()));
    CHECK(layout.width > 0);
}

TEST_CASE("each character comes from the first font in the stack that covers it") {
    const Font& inter = embeddedInterFont();
    const Font& mono = *genericFont(GenericFallback::Monospace);
    const auto cp = onlyIn(inter, mono);
    const auto layout = layoutText(run("Ab" + utf8(cp) + "c"), TextFonts{{&inter, &mono}, GenericFallback::None});
    REQUIRE(layout.glyphs.size() == 4);
    CHECK(layout.glyphs[0].key.instance == instanceOf(inter));
    CHECK(layout.glyphs[2].key.instance == instanceOf(mono));
    CHECK(layout.glyphs[3].key.instance == instanceOf(inter));
}

TEST_CASE("the generic family's bundled face ends the stack") {
    const Font& inter = embeddedInterFont();
    const Font& serif = *genericFont(GenericFallback::Serif);
    const auto cp = onlyIn(inter, serif);
    const auto layout = layoutText(run("a" + utf8(cp)), TextFonts{{&inter}, GenericFallback::Serif});
    REQUIRE(layout.glyphs.size() == 2);
    CHECK(layout.glyphs[1].key.instance == instanceOf(serif));
    CHECK(genericFont(GenericFallback::SansSerif) == &inter);
    CHECK(genericFont(GenericFallback::SystemUi) == &inter);
    CHECK(genericFont(GenericFallback::None) == nullptr);
}

TEST_CASE("fonts whose faces are not registered are skipped") {
    const Font missing = unregistered();
    const Font& mono = *genericFont(GenericFallback::Monospace);
    const auto text = run("Hola");
    const auto layout = layoutText(text, TextFonts{{&missing}, GenericFallback::Monospace});
    CHECK(instances(layout) == std::set<std::uint32_t>{instanceOf(mono)});
    CHECK_THAT(layout.width, WithinAbs(layoutText(text, TextFonts{{&mono}, GenericFallback::None}).width, 1e-9));
    CHECK_FALSE(layoutText(text, TextFonts{{&missing}, GenericFallback::None}).glyphs.empty());
}

TEST_CASE("a combining mark stays with the font of its base letter") {
    const auto layout = layoutText(run("e\xcc\x81"), TextFonts{{&embeddedInterFont()}, GenericFallback::Serif});
    CHECK(instances(layout) == std::set<std::uint32_t>{instanceOf(embeddedInterFont())});
}

TEST_CASE("axis values a fallback font does not allow are clamped to its domain") {
    TextRun text = run("Hola");
    text.axes = {{"wght", 900}, {"slnt", -10}};
    const Font& mono = *genericFont(GenericFallback::Monospace);
    const auto* wght = mono.axis("wght");
    REQUIRE(wght);
    REQUIRE(wght->domain.max < 900);
    const auto layout = layoutText(text, TextFonts{{&mono}, GenericFallback::None});
    TextRun heaviest = run("Hola");
    heaviest.axes = {{"wght", wght->domain.max}};
    CHECK_THAT(layout.width, WithinAbs(layoutText(heaviest, TextFonts{{&mono}, GenericFallback::None}).width, 1e-9));
}

TEST_CASE("widths of Inter text are unchanged by stack resolution") {
    CHECK_THAT(layoutText([] { auto t = run("AVATAR Tower"); t.fontSize = 100; t.axes = {{"wght", 700}}; return t; }()).width,
               WithinAbs(721.41, 0.01));
}
