#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "../src/sha256.hpp"
#include "vc/json.hpp"
#include "vc/native.h"
#include "vc/session.hpp"
#include "vc/text.hpp"

using namespace vc;
using Catch::Matchers::WithinAbs;

namespace {

std::vector<std::uint8_t> bytesOf(const char* file) {
    std::ifstream in(std::string(VC_FONT_DIR) + "/" + file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

/// Registers a bundled font file's bytes under `face`.
void registerFile(const std::string& face, const char* file) {
    const auto bytes = bytesOf(file);
    REQUIRE(registerFontFace(face, bytes, sha256Hex(bytes)) == nullptr);
}

Font familyFont(const std::string& key, const std::string& version, std::vector<FontAxis> axes) {
    Font font;
    font.id = Id{7, std::hash<std::string>{}(key + version)};
    font.identifier = "family";
    font.provider = "test";
    font.key = key;
    font.familyVersion = version;
    font.axes = std::move(axes);
    font.order = "a1";
    return font;
}

double widthWith(const Font& font, FontAxes axes, GenericFallback fallback = GenericFallback::None) {
    TextRun text;
    text.content = "Hola mundo";
    text.fontSize = 40;
    text.axes = std::move(axes);
    return layoutText(text, TextFonts{{&font}, fallback}).width;
}

/// The width of the same text in the bundled face for `generic`.
double widthOf(GenericFallback generic) {
    TextRun text;
    text.content = "Hola mundo";
    text.fontSize = 40;
    return layoutText(text, TextFonts{{genericFont(generic)}, GenericFallback::None}).width;
}

} // namespace

TEST_CASE("a family's ital axis selects its upright or italic file") {
    registerFile("test/ital@v1/upright", "JetBrainsMono.ttf");
    registerFile("test/ital@v1/italic", "SourceSerif4.ttf");
    REQUIRE(registerFontFamily("test", "ital", "v1", {{"test/ital@v1/upright", {{"ital", 0}}}, {"test/ital@v1/italic", {{"ital", 1}}}}) == nullptr);
    const Font font = familyFont("ital", "v1", {{"ital", {0, 0, {0, 1}}, 0.0}});
    CHECK_THAT(widthWith(font, {}), WithinAbs(widthOf(GenericFallback::Monospace), 1e-9));
    CHECK_THAT(widthWith(font, {{"ital", 1}}), WithinAbs(widthOf(GenericFallback::Serif), 1e-9));
}

TEST_CASE("a family of static files picks the closest weight") {
    registerFile("test/static@v1/400", "Inter.ttf");
    registerFile("test/static@v1/700", "JetBrainsMono.ttf");
    REQUIRE(registerFontFamily("test", "static", "v1", {{"test/static@v1/400", {{"wght", 400}}}, {"test/static@v1/700", {{"wght", 700}}}}) == nullptr);
    const Font font = familyFont("static", "v1", {{"wght", {0, 0, {400, 700}}, 400.0}});
    CHECK_THAT(widthWith(font, {{"wght", 400}}), WithinAbs(widthOf(GenericFallback::SansSerif), 1e-9));
    CHECK_THAT(widthWith(font, {{"wght", 700}}), WithinAbs(widthOf(GenericFallback::Monospace), 0.5));
}

TEST_CASE("declaring a family again needs the same files") {
    CHECK(registerFontFamily("test", "again", "v1", {{"test/again@v1/a", {}}}) == nullptr);
    CHECK(registerFontFamily("test", "again", "v1", {{"test/again@v1/a", {}}}) == nullptr);
    CHECK(registerFontFamily("test", "again", "v1", {{"test/again@v1/b", {}}}) != nullptr);
    CHECK(registerFontFamily("test", "again", "v2", {{"test/again@v2/b", {}}}) == nullptr);
    CHECK(registerFontFamily("", "again", "v1", {{"x", {}}}) != nullptr);
    CHECK(registerFontFamily("test", "empty", "v1", {}) != nullptr);
}

TEST_CASE("a declared file that is not loaded yet falls back, and layout follows once it is") {
    REQUIRE(registerFontFamily("test", "later", "v1", {{"test/later@v1/only", {}}}) == nullptr);
    Session session{IdGenerator{900}};
    Font font = familyFont("later", "v1", {});
    font.id = session.newId();
    REQUIRE(session.edit(CreateFont{font}));
    FontStack stack;
    stack.id = session.newId();
    stack.identifier = "later";
    stack.fonts = {font.id};
    stack.fallback = GenericFallback::Serif;
    stack.order = "a1";
    REQUIRE(session.edit(CreateFontStack{stack}));
    Node node;
    node.id = session.newId();
    node.type = NodeType::Text;
    node.order = "a0";
    node.text.content = "Hola mundo";
    node.text.fontSize = 40;
    node.text.stack = stack.id;
    node.widthSizing = node.heightSizing = Sizing::Hug;
    REQUIRE(session.edit(CreateNode{node}));
    const double before = session.document().box(node.id).w;
    CHECK_THAT(before, WithinAbs(widthOf(GenericFallback::Serif), 1e-9));
    registerFile("test/later@v1/only", "JetBrainsMono.ttf");
    CHECK_THAT(session.document().box(node.id).w, WithinAbs(widthOf(GenericFallback::Monospace), 1e-9));
}

TEST_CASE("capabilities are read from font bytes without registering them") {
    const auto read = fontCapabilitiesJson(bytesOf("Inter.ttf"));
    REQUIRE(read);
    CHECK(read->find(R"("tag":"wght")") != std::string::npos);
    CHECK(read->find(R"("tag":"slnt")") != std::string::npos);
    CHECK(read->find(R"("kern")") != std::string::npos);
    const std::vector<std::uint8_t> garbage(512, 0x41);
    CHECK_FALSE(fontCapabilitiesJson(garbage));
}

TEST_CASE("familyVersion round trips through fonts JSON") {
    Font font = familyFont("versioned", "cc39c4c2", {});
    const auto read = fontFromJson(R"({"id":")" + font.id.toString() +
        R"(","identifier":"family","provider":"test","key":"versioned","familyVersion":"cc39c4c2","capabilities":{"axes":[],"features":[]},"order":"a1"})");
    REQUIRE(read);
    CHECK(read->familyVersion == "cc39c4c2");
    Document document;
    REQUIRE(document.apply(CreateFont{*read}));
    CHECK(fontsToJson(document).find(R"("familyVersion":"cc39c4c2")") != std::string::npos);
    font.familyVersion = std::string(129, 'a');
    CHECK(fontProblem(font) != nullptr);
}

TEST_CASE("the native library declares families and reads capabilities") {
    CHECK(vc_register_font_family("test", "native", "v1", R"([{"face":"test/native@v1/a","fixed":{"ital":0}}])") == 1);
    CHECK(vc_last_error() == nullptr);
    CHECK(vc_register_font_family("test", "native", "v1", R"([{"face":"test/native@v1/b"}])") == 0);
    CHECK(vc_last_error() != nullptr);
    CHECK(vc_register_font_family("test", "native", "v1", "not json") == 0);
    const auto bytes = bytesOf("JetBrainsMono.ttf");
    char* read = vc_font_capabilities(bytes.data(), bytes.size());
    REQUIRE(read);
    CHECK(std::string(read).find(R"("tag":"wght")") != std::string::npos);
    vc_free(read);
    const unsigned char junk[] = {1, 2, 3};
    CHECK(vc_font_capabilities(junk, sizeof junk) == nullptr);
}
