#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "vc/editor.hpp"
#include "vc/json.hpp"
#include "vc/session.hpp"
#include "vc/text.hpp"

using namespace vc;
using Catch::Matchers::WithinAbs;

namespace {

struct Fixture {
    Session session{IdGenerator{1200}};
    Camera camera;
    Editor editor{session, camera};
    Id font = session.newId();
    Id stack = session.newId();
    Id text = session.newId();

    explicit Fixture(GenericFallback fallback, const char* content = "Hola mundo") {
        Font f = embeddedInterFont();
        f.id = font;
        f.identifier = "brand";
        f.order = "a1";
        REQUIRE(session.edit(CreateFont{f}));
        FontStack s;
        s.id = stack;
        s.identifier = "brand";
        s.fonts = {font};
        s.fallback = fallback;
        s.order = "a1";
        REQUIRE(session.edit(CreateFontStack{s}));
        Node node;
        node.id = text;
        node.type = NodeType::Text;
        node.order = "a0";
        node.text.content = content;
        node.text.fontSize = 40;
        node.text.stack = stack;
        node.widthSizing = node.heightSizing = Sizing::Hug;
        REQUIRE(session.edit(CreateNode{node}));
    }
    TextLayout layout() const { return session.document().layoutText(session.document().find(text)->text); }
    double width() const { return session.document().box(text).w; }
};

double serifWidth(const char* content) {
    TextRun run;
    run.content = content;
    run.fontSize = 40;
    return layoutText(run, TextFonts{{genericFont(GenericFallback::Serif)}, GenericFallback::None}).width;
}

} // namespace

TEST_CASE("an unavailable font falls back to its stack's generic face and relayouts") {
    Fixture f{GenericFallback::Serif};
    const double available = f.width();
    CHECK_FALSE(f.layout().approximated);
    REQUIRE(f.editor.setFontAvailable(f.font, false));
    CHECK(f.session.document().unavailableFonts().contains(f.font));
    CHECK_THAT(f.width(), WithinAbs(serifWidth("Hola mundo"), 1e-9));
    CHECK(f.layout().approximated);
    CHECK_FALSE(f.layout().missing);
    REQUIRE(f.editor.setFontAvailable(f.font, true));
    CHECK_THAT(f.width(), WithinAbs(available, 1e-9));
    CHECK_FALSE(f.layout().approximated);
}

TEST_CASE("an unavailable font with no generic draws missing-glyph marks, not a substitute") {
    Fixture f{GenericFallback::None};
    REQUIRE(f.editor.setFontAvailable(f.font, false));
    const auto layout = f.layout();
    CHECK(layout.missing);
    CHECK_FALSE(layout.approximated);
    REQUIRE(layout.glyphs.size() == 9);
    for (const auto& glyph : layout.glyphs) CHECK(glyph.key.glyph == 0);
}

TEST_CASE("characters no font covers are reported as missing") {
    Fixture f{GenericFallback::None, "Hola \xe4\xb8\xad"};
    CHECK(f.layout().missing);
    CHECK_FALSE(f.layout().approximated);
}

TEST_CASE("the bundled Inter and unknown fonts cannot be marked unavailable") {
    Fixture f{GenericFallback::Serif};
    CHECK_FALSE(f.editor.setFontAvailable(kInterFontId, false));
    CHECK_FALSE(f.editor.setFontAvailable(Id{9, 9}, false));
    CHECK(f.session.document().unavailableFonts().empty());
}

TEST_CASE("availability is view state that copies with the document but stays out of snapshots and history") {
    Fixture f{GenericFallback::Serif};
    const auto before = snapshotToJson(f.session.document());
    REQUIRE(f.editor.setFontAvailable(f.font, false));
    CHECK(snapshotToJson(f.session.document()) == before);
    CHECK(f.session.document().detachedCopy()->unavailableFonts().contains(f.font));
    REQUIRE(f.editor.undo());
    CHECK(f.session.document().unavailableFonts().contains(f.font));
}
