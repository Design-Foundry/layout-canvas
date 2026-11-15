#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <algorithm>
#include "vc/editor.hpp"
#include "vc/json.hpp"
#include "vc/session.hpp"
#include "vc/text.hpp"

using namespace vc;
using Catch::Matchers::WithinAbs;

namespace {

Font customFont(Id id, std::vector<double> wghtValues = {400, 700}) {
    Font font;
    font.id = id;
    font.identifier = "custom";
    font.displayName = "Custom";
    font.provider = "embedded";
    font.key = "inter";
    font.axes.push_back({"wght", {0, 0, std::move(wghtValues)}, 400.0});
    font.order = "a1";
    return font;
}

FontStack customStack(Id id, Id font) {
    FontStack stack;
    stack.id = id;
    stack.identifier = "custom-stack";
    stack.fonts = {font};
    stack.order = "a1";
    return stack;
}

struct Fixture {
    Session session{IdGenerator{300}};
    Camera camera;
    Editor editor{session, camera};
    Id font = session.newId();
    Id stack = session.newId();
    Id text;

    void addFontAndStack() {
        REQUIRE(session.edit(CreateFont{customFont(font)}));
        REQUIRE(session.edit(CreateFontStack{customStack(stack, font)}));
    }
    void addText(FontAxes axes = {}, FontFeatures features = {}) {
        Node node;
        text = node.id = session.newId();
        node.type = NodeType::Text;
        node.order = "a0";
        node.text.content = "Hola";
        node.text.stack = stack;
        node.text.axes = std::move(axes);
        node.text.features = std::move(features);
        REQUIRE(session.edit(CreateNode{node}));
    }
    bool setAxes(FontAxes axes) {
        NodePatch patch;
        patch.text = session.document().find(text)->text;
        patch.text->axes = std::move(axes);
        return session.edit(SetProps{text, patch});
    }
    bool setFeatures(FontFeatures features) {
        NodePatch patch;
        patch.text = session.document().find(text)->text;
        patch.text->features = std::move(features);
        return session.edit(SetProps{text, patch});
    }
};

Document load(const std::string& snapshot) {
    const auto ops = snapshotFromJson(snapshot);
    REQUIRE(ops.has_value());
    Document document;
    for (const auto& op : *ops) REQUIRE(document.apply(op));
    return document;
}

} // namespace

TEST_CASE("the built-in Inter font and stack carry the capabilities read from the file") {
    Document document;
    const auto* font = document.findFont(kInterFontId);
    REQUIRE(font != nullptr);
    CHECK(font->provider == "embedded");
    CHECK(font->key == "inter");
    const auto* wght = font->axis("wght");
    REQUIRE(wght != nullptr);
    CHECK(wght->domain.min == 100);
    CHECK(wght->domain.max == 900);
    CHECK(wght->defaultValue == 400);
    const auto* slnt = font->axis("slnt");
    REQUIRE(slnt != nullptr);
    CHECK(slnt->domain.min == -10);
    CHECK(slnt->domain.max == 0);
    CHECK(slnt->defaultValue == 0);
    CHECK(!font->features.empty());
    const auto* stack = document.findFontStack(kInterStackId);
    REQUIRE(stack != nullptr);
    CHECK(stack->fonts == std::vector<Id>{kInterFontId});
}

TEST_CASE("the built-in Inter font and stack cannot be replaced or deleted") {
    Session session{IdGenerator{301}};
    auto font = *session.document().findFont(kInterFontId);
    font.displayName = "Other";
    CHECK_FALSE(session.edit(ReplaceFont{font}));
    CHECK_FALSE(session.edit(DeleteFont{kInterFontId}));
    auto stack = *session.document().findFontStack(kInterStackId);
    stack.displayName = "Other";
    CHECK_FALSE(session.edit(ReplaceFontStack{stack}));
    CHECK_FALSE(session.edit(DeleteFontStack{kInterStackId}));
    CHECK(session.document().findFont(kInterFontId)->displayName == "Inter");
}

TEST_CASE("a discrete axis domain accepts only its values") {
    Fixture f;
    f.addFontAndStack();
    f.addText();
    CHECK(f.setAxes({{"wght", 700}}));
    CHECK_FALSE(f.setAxes({{"wght", 500}}));
    CHECK_FALSE(f.setAxes({{"slnt", -5}}));
    CHECK(f.session.document().find(f.text)->text.axes == FontAxes{{"wght", 700}});
}

TEST_CASE("features the primary font does not declare are refused") {
    Fixture f;
    f.addFontAndStack();
    f.addText();
    CHECK_FALSE(f.setFeatures({{"tnum", true}}));

    auto font = customFont(f.font);
    font.features = {"tnum"};
    REQUIRE(f.session.edit(ReplaceFont{font}));
    CHECK(f.setFeatures({{"tnum", true}}));
    CHECK_FALSE(f.setFeatures({{"liga", true}}));
}

TEST_CASE("fonts and stacks are removed only when nothing depends on them") {
    Fixture f;
    f.addFontAndStack();
    const auto before = fontsToJson(f.session.document());

    CHECK_FALSE(f.session.edit(DeleteFont{f.font}));
    f.addText({{"wght", 700}});
    CHECK_FALSE(f.session.edit(DeleteFontStack{f.stack}));
    REQUIRE(f.editor.undo());
    CHECK(fontsToJson(f.session.document()) == before);

    REQUIRE(f.session.edit(DeleteFontStack{f.stack}));
    CHECK(f.session.document().findFontStack(f.stack) == nullptr);
    REQUIRE(f.editor.undo());
    CHECK(fontsToJson(f.session.document()) == before);
    REQUIRE(f.session.edit(DeleteFontStack{f.stack}));
    REQUIRE(f.session.edit(DeleteFont{f.font}));
    CHECK(f.session.document().findFont(f.font) == nullptr);
    REQUIRE(f.editor.undo());
    CHECK(f.session.document().findFont(f.font) != nullptr);
    REQUIRE(f.editor.undo());
    CHECK(fontsToJson(f.session.document()) == before);
}

TEST_CASE("replacing a font or stack is refused when a text would become invalid") {
    Fixture f;
    f.addFontAndStack();
    f.addText({{"wght", 700}});
    const auto before = fontsToJson(f.session.document());

    CHECK_FALSE(f.session.edit(ReplaceFont{customFont(f.font, {400, 500})}));
    CHECK(fontsToJson(f.session.document()) == before);

    const Id other = f.session.newId();
    auto narrow = customFont(other);
    narrow.axes.clear();
    narrow.identifier = "narrow";
    REQUIRE(f.session.edit(CreateFont{narrow}));
    auto stack = customStack(f.stack, other);
    CHECK_FALSE(f.session.edit(ReplaceFontStack{stack}));

    auto wider = customFont(f.font, {400, 500, 700});
    REQUIRE(f.session.edit(ReplaceFont{wider}));
    CHECK(f.setAxes({{"wght", 500}}));
    REQUIRE(f.editor.undo());
    REQUIRE(f.editor.undo());
    CHECK(f.session.document().findFont(f.font)->axis("wght")->domain.values == std::vector<double>{400, 700});

    stack.fonts = {f.font, other};
    stack.displayName = "Renamed";
    REQUIRE(f.session.edit(ReplaceFontStack{stack}));
    CHECK(f.session.document().findFontStack(f.stack)->displayName == "Renamed");
    REQUIRE(f.editor.undo());
    CHECK(f.session.document().findFontStack(f.stack)->displayName.empty());
}

TEST_CASE("a text style with axes or features but no stack is refused") {
    Fixture f;
    f.addFontAndStack();
    f.addText();
    NodePatch patch;
    patch.textStyle = TextStyle{};
    patch.textStyle->axes = FontAxes{{"wght", 700}};
    CHECK_FALSE(f.session.edit(SetProps{f.text, patch}));
    patch.textStyle = TextStyle{};
    patch.textStyle->features = FontFeatures{{"tnum", true}};
    CHECK_FALSE(f.session.edit(SetProps{f.text, patch}));
    CHECK_FALSE(patchFromJson(R"({"textStyle":{"axes":{"wght":700}}})").has_value());
}

TEST_CASE("a snapshot round trips custom fonts, stacks, axes and features at version 25") {
    Fixture f;
    auto font = customFont(f.font);
    font.features = {"tnum", "liga"};
    font.sha256 = std::string(64, 'a');
    REQUIRE(f.session.edit(CreateFont{font}));
    REQUIRE(f.session.edit(CreateFontStack{customStack(f.stack, f.font)}));
    f.addText({{"wght", 700}}, {{"tnum", true}, {"liga", false}});

    const auto saved = snapshotToJson(f.session.document());
    CHECK(saved.find("\"version\":25") != std::string::npos);
    CHECK(saved.find("\"fonts\"") != std::string::npos);
    CHECK(saved.find("\"fontStacks\"") != std::string::npos);

    const auto loaded = load(saved);
    CHECK(*loaded.findFont(f.font) == *f.session.document().findFont(f.font));
    CHECK(*loaded.findFontStack(f.stack) == *f.session.document().findFontStack(f.stack));
    const auto& text = loaded.find(f.text)->text;
    CHECK(text.stack == f.stack);
    CHECK(text.axes == FontAxes{{"wght", 700}});
    CHECK(text.features == FontFeatures{{"tnum", true}, {"liga", false}});
    CHECK(snapshotToJson(loaded) == saved);
}

TEST_CASE("older snapshots refuse the font model and map legacy font ids to Inter axes") {
    const auto snapshot = [](int version, const std::string& textFields) {
        return R"({"version":)" + std::to_string(version) + R"(,"modes":[],"tokens":[],"nodes":[{"id":"0192a4b1-7c3d-4e5f-8a6b-000000000002","type":"text","order":"a0","text":{"content":"a",)" +
               textFields + R"(,"fontSize":16,"lineHeight":1.2,"letterSpacing":0,"align":"left"}}]})";
    };
    const std::string stackId = "00000000-0000-0000-0000-000000000101";

    CHECK_FALSE(snapshotFromJson(snapshot(24, R"("stack":")" + stackId + R"(")")).has_value());
    CHECK_FALSE(snapshotFromJson(snapshot(24, R"("stack":")" + stackId + R"(","axes":{"wght":700})")).has_value());

    const auto document = load(snapshot(24, R"("font":"inter-bold-v1")"));
    const auto id = *Id::parse("0192a4b1-7c3d-4e5f-8a6b-000000000002");
    const auto& text = document.find(id)->text;
    CHECK(text.stack == kInterStackId);
    CHECK(text.axes == FontAxes{{"wght", 700}});

    const auto modern = load(snapshot(25, R"("stack":")" + stackId + R"(","axes":{"wght":650})"));
    CHECK(modern.find(id)->text.axes == FontAxes{{"wght", 650}});
    CHECK_FALSE(snapshotFromJson(R"({"version":25,"modes":[],"tokens":[],"fonts":{},"nodes":[]})").has_value());
}

TEST_CASE("font ops round trip through JSON") {
    Fixture f;
    auto font = customFont(f.font);
    font.features = {"tnum"};
    auto stack = customStack(f.stack, f.font);
    stack.fallback = GenericFallback::Serif;
    std::vector<OpBody> bodies{CreateFont{font}, DeleteFont{f.font}, ReplaceFont{font}, CreateFontStack{stack},
                               DeleteFontStack{f.stack}, ReplaceFontStack{stack}};
    std::vector<Op> ops;
    for (const auto& body : bodies) {
        const auto parsed = opFromJson(opKind(body), opToJson(body));
        REQUIRE(parsed.has_value());
        CHECK(*parsed == body);
        ops.push_back({f.session.newId(), body});
    }
    const auto parsed = opsFromJson(opsToJson(ops));
    REQUIRE(parsed.has_value());
    CHECK(*parsed == ops);
    CHECK(opKind(bodies[0]) == "CreateFont");
    CHECK(opKind(bodies[5]) == "ReplaceFontStack");
}

TEST_CASE("layout follows the stack's primary font") {
    SECTION("Inter bold keeps its width") {
        TextRun text;
        text.content = "AVATAR Tower";
        text.fontSize = 100;
        text.axes = {{"wght", 700}};
        CHECK_THAT(layoutText(text).width, WithinAbs(721.41, 0.01));
    }
    SECTION("a font whose face is not registered lays out nothing") {
        Fixture f;
        auto font = customFont(f.font);
        font.provider = "test";
        font.key = "missing";
        REQUIRE(f.session.edit(CreateFont{font}));
        REQUIRE(f.session.edit(CreateFontStack{customStack(f.stack, f.font)}));
        TextRun text;
        text.content = "Hola";
        text.stack = f.stack;
        CHECK(f.session.document().layoutText(text, -1, false).glyphs.empty());
        text.stack = kInterStackId;
        CHECK_FALSE(f.session.document().layoutText(text, -1, false).glyphs.empty());
    }
    SECTION("tabular numbers change the width of ones") {
        TextRun plain;
        plain.content = "1111";
        auto tabular = plain;
        tabular.features = {{"tnum", true}};
        CHECK(layoutText(tabular).width != layoutText(plain).width);
    }
}
