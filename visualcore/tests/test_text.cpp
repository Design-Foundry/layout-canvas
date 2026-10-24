#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <limits>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "vc/text.hpp"
#include "vc/editor.hpp"
#include "vc/draw_list.hpp"
#include "vc/json.hpp"

using namespace vc;
using Catch::Matchers::WithinAbs;

namespace {
struct PropertyFixture {
    Session session{IdGenerator{74}};
    Camera camera;
    Editor editor{session, camera};
    Id main, label, first, second;
    PropertyFixture() {
        Node m; main = m.id = session.newId(); m.type = NodeType::Component; m.order = "a0"; m.w = 200; m.h = 70;
        session.edit(CreateNode{m});
        Node t; label = t.id = session.newId(); t.type = NodeType::Text; t.parent = main; t.order = "a0";
        t.text.content = "Texto base"; t.widthSizing = t.heightSizing = Sizing::Hug;
        session.edit(CreateNode{t});
        first = *editor.createInstance(main); second = *editor.createInstance(main);
    }
    Id copy(Id instance) const { return session.document().viewChildren(instance).at(0); }
    ComponentDefinition definition(std::string value = "Continuar") const {
        ComponentDefinition d; d.texts.push_back({"label", label, std::move(value)}); return d;
    }
    std::string content(Id instance) const { return session.document().view(copy(instance))->text.content; }
};
}

TEST_CASE("Declared text defaults, independent instance choices and reset retain paint overrides") {
    PropertyFixture f;
    REQUIRE(f.editor.setComponentDefinition(f.main, f.definition()));
    REQUIRE(f.content(f.first) == "Continuar"); REQUIRE(f.content(f.second) == "Continuar");
    NodePatch choice; choice.componentValues = ComponentValues{{"label", std::string("Guardar dise\xc3\xb1o")}};
    REQUIRE(f.editor.setProps(f.first, choice));
    NodePatch paint; paint.opacity = .6; REQUIRE(f.editor.setProps(f.copy(f.first), paint));
    REQUIRE(f.editor.setComponentDefinition(f.main, f.definition("Siguiente")));
    REQUIRE(f.content(f.first) == "Guardar dise\xc3\xb1o"); REQUIRE(f.content(f.second) == "Siguiente");
    choice.componentValues = ComponentValues{}; REQUIRE(f.editor.setProps(f.first, choice));
    REQUIRE(f.content(f.first) == "Siguiente");
    REQUIRE(std::get<double>(f.session.document().view(f.copy(f.first))->opacity) == .6);
    REQUIRE(f.editor.undo()); REQUIRE(f.content(f.first) == "Guardar dise\xc3\xb1o");
    REQUIRE(f.editor.redo()); REQUIRE(f.content(f.first) == "Siguiente");
    choice.componentValues = ComponentValues{{"label", std::string{}}}; REQUIRE(f.editor.setProps(f.first, choice));
    REQUIRE(f.content(f.first).empty());
}

TEST_CASE("Declared text uses snapshot 6 and accepts long strings without weakening axes") {
    PropertyFixture f;
    auto d = f.definition(); d.axes.push_back({"size", {"small", "large"}, "small"});
    REQUIRE(f.editor.setComponentDefinition(f.main, d));
    NodePatch choice; choice.componentValues = ComponentValues{{"label", std::string(200, 'A')}, {"size", std::string("large")}};
    REQUIRE(f.editor.setProps(f.first, choice));
    const auto saved = snapshotToJson(f.session.document()); REQUIRE(saved.find("\"version\":6") != std::string::npos);
    auto ops = snapshotFromJson(saved); REQUIRE(ops.has_value()); Document loaded;
    for (const auto& op : *ops) REQUIRE(loaded.apply(op));
    REQUIRE(loaded.view(loaded.viewChildren(f.first).at(0))->text.content == std::string(200, 'A'));
    auto old = saved; old.replace(old.find("\"version\":6"), 11, "\"version\":5"); REQUIRE_FALSE(snapshotFromJson(old).has_value());
    choice.componentValues = ComponentValues{{"size", std::string(200, 'A')}}; REQUIRE_FALSE(f.editor.setProps(f.first, choice));
    choice.componentValues = ComponentValues{{"label", true}}; REQUIRE_FALSE(f.editor.setProps(f.first, choice));
    choice.componentValues = ComponentValues{{"label", std::string("\xf0\x9f\x98\x80")}}; REQUIRE_FALSE(f.editor.setProps(f.first, choice));
}

TEST_CASE("Declaring text refuses existing content overrides and protects its actual anatomy") {
    PropertyFixture f;
    NodePatch local; local.text = f.session.document().find(f.label)->text; local.text->content = "Personalizado";
    REQUIRE(f.editor.setProps(f.copy(f.first), local));
    REQUIRE_FALSE(f.editor.setComponentDefinition(f.main, f.definition()));
    REQUIRE(f.editor.resetOverrides(f.copy(f.first)));
    REQUIRE(f.editor.setComponentDefinition(f.main, f.definition()));
    const auto before = snapshotToJson(f.session.document());
    REQUIRE_FALSE(f.editor.setProps(f.copy(f.first), local));
    REQUIRE_FALSE(f.session.edit(MoveNode{f.label, {}, "a9"}));
    REQUIRE_FALSE(f.session.edit(DeleteNode{f.label}));
    auto duplicate = f.definition(); duplicate.properties.push_back({"label", f.label, true});
    REQUIRE_FALSE(f.editor.setComponentDefinition(f.main, duplicate));
    auto invalid = f.definition("\xf0\x9f\x98\x80"); REQUIRE_FALSE(f.editor.setComponentDefinition(f.main, invalid));
    REQUIRE(snapshotToJson(f.session.document()) == before);
}

TEST_CASE("Bound source typography remains editable without changing the raw fallback content") {
    PropertyFixture f; REQUIRE(f.editor.setComponentDefinition(f.main, f.definition("Predeterminado")));
    NodePatch style; style.text = f.session.document().view(f.label)->text; style.text->fontSize = 32;
    REQUIRE(f.editor.setProps(f.label, style));
    REQUIRE(f.session.document().find(f.label)->text.content == "Texto base");
    REQUIRE(f.content(f.first) == "Predeterminado");
    REQUIRE(f.session.document().view(f.copy(f.first))->text.fontSize == 32);
    style.text->content = "Cambiar por fuera"; REQUIRE_FALSE(f.editor.setProps(f.label, style));
    NodePatch value; value.componentValues = ComponentValues{{"label", std::string("Propio")}}; REQUIRE(f.editor.setProps(f.first, value));
    REQUIRE_FALSE(f.editor.setComponentDefinition(f.main, {}));
}

TEST_CASE("Bound text typography overrides stay independent of declared content and its defaults") {
    PropertyFixture f; REQUIRE(f.editor.setComponentDefinition(f.main, f.definition()));
    NodePatch style; style.text = f.session.document().view(f.copy(f.first))->text; style.text->fontSize = 32;
    REQUIRE(f.editor.setProps(f.copy(f.first), style));
    const auto& stored = f.session.document().find(f.first)->overrides.at(0).patch;
    REQUIRE(stored.textStyle.has_value()); REQUIRE_FALSE(stored.text.has_value());
    REQUIRE(f.editor.setComponentDefinition(f.main, f.definition("Siguiente")));
    REQUIRE(f.content(f.first) == "Siguiente");
    REQUIRE(f.session.document().view(f.copy(f.first))->text.fontSize == 32);
    REQUIRE(f.session.document().view(f.copy(f.second))->text.fontSize == 16);
    NodePatch choice; choice.componentValues = ComponentValues{{"label", std::string("Personalizado")}};
    REQUIRE(f.editor.setProps(f.first, choice));
    REQUIRE(f.content(f.first) == "Personalizado");
    REQUIRE(f.session.document().view(f.copy(f.first))->text.fontSize == 32);
    auto ops = snapshotFromJson(snapshotToJson(f.session.document())); REQUIRE(ops.has_value()); Document loaded;
    for (const auto& op : *ops) REQUIRE(loaded.apply(op));
    REQUIRE(loaded.view(loaded.viewChildren(f.first).at(0))->text.content == "Personalizado");
    REQUIRE(loaded.view(loaded.viewChildren(f.first).at(0))->text.fontSize == 32);
    REQUIRE(f.editor.resetOverrides(f.copy(f.first)));
    REQUIRE(f.content(f.first) == "Personalizado");
    REQUIRE(f.session.document().view(f.copy(f.first))->text.fontSize == 16);
    REQUIRE(f.editor.undo()); REQUIRE(f.session.document().view(f.copy(f.first))->text.fontSize == 32);
}

TEST_CASE("A bound instance overrides only edited typography and inherits later source metrics") {
    PropertyFixture f; REQUIRE(f.editor.setComponentDefinition(f.main, f.definition()));
    NodePatch font; font.text = f.session.document().view(f.copy(f.first))->text; font.text->fontSize = 32;
    REQUIRE(f.editor.setProps(f.copy(f.first), font));
    const auto stored = f.session.document().find(f.first)->overrides.at(0).patch.textStyle;
    REQUIRE(stored->fontSize == 32); REQUIRE_FALSE(stored->lineHeight); REQUIRE_FALSE(stored->align);
    NodePatch source; source.text = f.session.document().view(f.label)->text; source.text->lineHeight = 2; source.text->align = TextAlign::Center;
    REQUIRE(f.editor.setProps(f.label, source));
    auto view = f.session.document().view(f.copy(f.first));
    REQUIRE(view->text.fontSize == 32); REQUIRE(view->text.lineHeight == 2); REQUIRE(view->text.align == TextAlign::Center);
    NodePatch spacing; spacing.text = view->text; spacing.text->letterSpacing = 3;
    REQUIRE(f.editor.setProps(f.copy(f.first), spacing));
    REQUIRE(f.session.document().find(f.first)->overrides.at(0).patch.textStyle->fontSize == 32);
    REQUIRE(f.editor.undo()); REQUIRE(f.session.document().view(f.copy(f.first))->text.letterSpacing == 0);
    REQUIRE(f.editor.redo()); REQUIRE(f.session.document().view(f.copy(f.first))->text.letterSpacing == 3);
    REQUIRE(f.editor.resetOverrides(f.copy(f.first)));
    REQUIRE(f.session.document().view(f.copy(f.first))->text.fontSize == 16);
    REQUIRE(f.session.document().view(f.copy(f.first))->text.lineHeight == 2);
}

TEST_CASE("Partial text variant fields compose across axes and require snapshot 7") {
    PropertyFixture f; auto d = f.definition();
    d.axes = {{"size", {"small", "large"}, "small"}, {"tone", {"normal", "wide"}, "normal"}};
    NodePatch large; large.textStyle = TextStyle{}; large.textStyle->fontSize = 30;
    NodePatch wide; wide.textStyle = TextStyle{}; wide.textStyle->lineHeight = 2; wide.textStyle->letterSpacing = 3; wide.textStyle->align = TextAlign::Center;
    d.rules = {{{{"size", "large"}}, f.label, large}, {{{"tone", "wide"}}, f.label, wide}};
    REQUIRE(f.editor.setComponentDefinition(f.main, d));
    NodePatch choice; choice.componentValues = ComponentValues{{"size", std::string("large")}, {"tone", std::string("wide")}};
    REQUIRE(f.editor.setProps(f.first, choice));
    auto view = f.session.document().view(f.copy(f.first));
    REQUIRE(view->text.fontSize == 30); REQUIRE(view->text.lineHeight == 2); REQUIRE(view->text.letterSpacing == 3); REQUIRE(view->text.align == TextAlign::Center);
    const auto saved = snapshotToJson(f.session.document()); REQUIRE(saved.find("\"version\":7") != std::string::npos);
    auto ops = snapshotFromJson(saved); REQUIRE(ops.has_value()); Document loaded;
    for (const auto& op : *ops) REQUIRE(loaded.apply(op));
    REQUIRE(loaded.view(loaded.viewChildren(f.first).at(0))->text == view->text);
    auto old = saved; old.replace(old.find("\"version\":7"), 11, "\"version\":6"); REQUIRE_FALSE(snapshotFromJson(old).has_value());
    NodePatch invalid; invalid.textStyle = TextStyle{}; REQUIRE_FALSE(f.editor.setProps(f.label, invalid));
    invalid.textStyle->fontSize = 0; REQUIRE_FALSE(f.editor.setProps(f.label, invalid));
    REQUIRE_FALSE(patchFromJson(R"({"textStyle":{}})").has_value());
    REQUIRE_FALSE(patchFromJson(R"({"textStyle":{"font":"another"}})").has_value());
    REQUIRE_FALSE(patchFromJson(R"({"textStyle":{"align":null}})").has_value());
    REQUIRE(patchFromJson(R"({"textStyle":{"letterSpacing":0}})").has_value());
}

TEST_CASE("Removing one conditional text metric restores inheritance without discarding other authored fields") {
    PropertyFixture f;
    auto d = f.definition(); d.axes = {{"tone", {"normal", "airy"}, "normal"}};
    NodePatch style; style.textStyle = TextStyle{};
    style.textStyle->fontSize = 32; style.textStyle->lineHeight = 2; style.textStyle->letterSpacing = 3;
    d.rules = {{{{"tone", "airy"}}, f.label, style}};
    REQUIRE(f.editor.setComponentDefinition(f.main, d));
    NodePatch choice; choice.componentValues = ComponentValues{{"tone", std::string{"airy"}}, {"label", std::string{"Guardar"}}};
    REQUIRE(f.editor.setProps(f.first, choice));
    auto edited = d; edited.rules[0].patch.textStyle->lineHeight.reset();
    REQUIRE(f.editor.setComponentDefinition(f.main, edited));
    const Id copy = f.copy(f.first);
    REQUIRE(f.session.document().view(copy)->text.lineHeight == 1.4);
    REQUIRE(f.session.document().view(copy)->text.fontSize == 32);
    REQUIRE(f.session.document().view(copy)->text.letterSpacing == 3);
    REQUIRE(f.content(f.first) == "Guardar");
    REQUIRE(f.editor.undo()); REQUIRE(f.session.document().view(copy)->text.lineHeight == 2);
    REQUIRE(f.editor.redo()); REQUIRE(f.session.document().view(copy)->text.lineHeight == 1.4);
    const auto ops = snapshotFromJson(snapshotToJson(f.session.document())); REQUIRE(ops.has_value());
    Document loaded; for (const auto& op : *ops) REQUIRE(loaded.apply(op));
    REQUIRE(loaded.find(f.main)->definition == edited);
    REQUIRE(loaded.view(copy)->text.lineHeight == 1.4);
    NodePatch local; local.textStyle = TextStyle{}; local.textStyle->fontSize = 40;
    REQUIRE(f.editor.setProps(copy, local));
    edited.rules.clear(); REQUIRE(f.editor.setComponentDefinition(f.main, edited));
    REQUIRE(f.session.document().view(copy)->text.fontSize == 40);
    REQUIRE(f.session.document().view(copy)->text.letterSpacing == 0);
    REQUIRE(f.editor.resetOverrides(copy));
    REQUIRE(f.session.document().view(copy)->text.fontSize == 16);
    REQUIRE(f.content(f.first) == "Guardar");
}

TEST_CASE("Unbinding a styled text layer permits content edits without mixed override forms") {
    PropertyFixture f; REQUIRE(f.editor.setComponentDefinition(f.main, f.definition()));
    NodePatch font; font.text = f.session.document().view(f.copy(f.first))->text; font.text->fontSize = 32;
    REQUIRE(f.editor.setProps(f.copy(f.first), font));
    REQUIRE(f.editor.setComponentDefinition(f.main, {}));
    NodePatch content; content.text = f.session.document().view(f.copy(f.first))->text; content.text->content = "Libre";
    REQUIRE(f.editor.setProps(f.copy(f.first), content));
    const auto& patch = f.session.document().find(f.first)->overrides.at(0).patch;
    REQUIRE(patch.text); REQUIRE_FALSE(patch.textStyle); REQUIRE(patch.text->fontSize == 32); REQUIRE(patch.text->content == "Libre");
    const auto before = snapshotToJson(f.session.document());
    NodePatch empty; empty.textStyle = TextStyle{};
    REQUIRE_FALSE(f.editor.setProps(f.copy(f.first), empty));
    REQUIRE(snapshotToJson(f.session.document()) == before);
    NodePatch spacing; spacing.textStyle = TextStyle{}; spacing.textStyle->letterSpacing = 2;
    REQUIRE(f.editor.setProps(f.copy(f.first), spacing));
    REQUIRE(f.session.document().view(f.copy(f.first))->text.content == "Libre");
    REQUIRE(f.session.document().view(f.copy(f.first))->text.letterSpacing == 2);
    REQUIRE(f.editor.undo());
    REQUIRE(f.editor.undo()); REQUIRE(f.content(f.first) == "Texto base");
    REQUIRE(f.editor.undo()); REQUIRE(f.content(f.first) == "Continuar");
}

TEST_CASE("Native wrapping preserves glyphs, explicit lines and long words at narrow widths") {
    TextRun text; text.content = "AV AV";
    TextRun word = text; word.content = "AV ";
    const auto width = layoutText(word).width + .1;
    const auto wrapped = layoutText(text, width, true);
    REQUIRE(wrapped.height == text.fontSize * text.lineHeight * 2);
    REQUIRE(wrapped.glyphs.size() == layoutText(text).glyphs.size());
    REQUIRE(layoutText(text, width).height == text.fontSize * text.lineHeight);
    text.content = "WWWW";
    REQUIRE(layoutText(text, 0, true).height == text.fontSize * text.lineHeight * 4);
    REQUIRE(layoutText(text, 1, true).glyphs.size() == 4);
    text.content = "A\n\nB\n";
    REQUIRE(layoutText(text, 100, true).height == text.fontSize * text.lineHeight * 4);
    text.content = "  A  B ";
    REQUIRE(layoutText(text, 20, true).glyphs.size() == 2);
    text.content = "AV AV"; text.align = TextAlign::Right;
    const auto right = layoutText(text, width, true);
    text.align = TextAlign::Left;
    const auto left = layoutText(text, width, true);
    REQUIRE(right.glyphs.back().box.x > left.glyphs.back().box.x);
}

TEST_CASE("Wrapping inherits independently, persists in snapshot 8 and protects non-text targets") {
    PropertyFixture f; auto d = f.definition("Texto largo para continuar");
    REQUIRE(f.editor.setComponentDefinition(f.main, d));
    NodePatch wrap; wrap.textWrap = true; wrap.w = 70; wrap.widthSizing = Sizing::Fixed;
    REQUIRE(f.editor.setProps(f.label, wrap));
    const auto copy = f.copy(f.first);
    REQUIRE(f.session.document().box(copy).h > 16 * 1.4);
    NodePatch local; local.textWrap = false;
    REQUIRE(f.editor.setProps(copy, local));
    REQUIRE(f.session.document().box(copy).h == 16 * 1.4);
    const auto saved = snapshotToJson(f.session.document());
    REQUIRE(saved.find("\"version\":8") != std::string::npos);
    auto ops = snapshotFromJson(saved); REQUIRE(ops.has_value());
    Document loaded; for (const auto& op : *ops) REQUIRE(loaded.apply(op));
    REQUIRE_FALSE(loaded.view(copy)->textWrap);
    auto old = saved; old.replace(old.find("\"version\":8"),11,"\"version\":7"); REQUIRE_FALSE(snapshotFromJson(old).has_value());
    REQUIRE(f.editor.resetOverrides(copy)); REQUIRE(f.session.document().view(copy)->textWrap);
    REQUIRE(f.editor.undo()); REQUIRE_FALSE(f.session.document().view(copy)->textWrap);
    REQUIRE(f.editor.redo()); REQUIRE(f.session.document().view(copy)->textWrap);
    REQUIRE_FALSE(f.editor.setProps(f.main, local));
    REQUIRE_FALSE(patchFromJson(R"({"textWrap":"true"})").has_value());
    Node invalid; invalid.id=f.session.newId(); invalid.type=NodeType::Instance; invalid.component=f.main; invalid.order="a3";
    invalid.overrides.push_back({f.main, local}); REQUIRE_FALSE(f.session.edit(CreateNode{invalid}));
}

TEST_CASE("Wrapped Fill text remeasures before column placement and row Hug height") {
    for (const auto direction : {LayoutType::Column, LayoutType::Row}) {
        Session session{IdGenerator{88}};
        Node frame; frame.id=session.newId(); frame.type=NodeType::Frame; frame.order="a0";
        frame.w=100; frame.heightSizing=Sizing::Hug; frame.layout.type=direction; frame.layout.gap=4.;
        REQUIRE(session.edit(CreateNode{frame}));
        Node text; text.id=session.newId(); text.parent=frame.id; text.type=NodeType::Text; text.order="a0";
        text.text.content="Uno dos tres cuatro cinco seis siete"; text.textWrap=true;
        text.w=300; text.widthSizing=Sizing::Fill; text.heightSizing=Sizing::Hug;
        REQUIRE(session.edit(CreateNode{text}));
        const auto expected=layoutText(text.text,100,true).height;
        REQUIRE_THAT(session.document().box(text.id).h, WithinAbs(expected,1e-8));
        REQUIRE_THAT(session.document().box(frame.id).h, WithinAbs(expected,1e-8));
        if (direction==LayoutType::Column) {
            Node footer; footer.id=session.newId(); footer.parent=frame.id; footer.order="a1"; footer.w=20; footer.h=10;
            REQUIRE(session.edit(CreateNode{footer}));
            REQUIRE_THAT(session.document().box(footer.id).y, WithinAbs(expected+4,1e-8));
            REQUIRE_THAT(session.document().box(frame.id).h, WithinAbs(expected+14,1e-8));
        }
        NodePatch wider; wider.w=200; REQUIRE(session.edit(SetProps{frame.id,wider}));
        REQUIRE(session.document().box(text.id).h < expected);
    }
}

TEST_CASE("Bundled bold uses a distinct trusted face with its own metrics, glyphs and kerning") {
    TextRun regular; regular.content="Continuar: José";
    auto bold=regular; bold.font="inter-bold-v1";
    REQUIRE(validText(bold));
    REQUIRE(layoutText(bold).width > layoutText(regular).width);
    REQUIRE(layoutText(bold).glyphs.front().uv.y > layoutText(regular).glyphs.front().uv.y);
    bold.content="AV"; const auto pair=layoutText(bold).width;
    bold.content="A"; const auto a=layoutText(bold).width; bold.content="V";
    REQUIRE(pair < a + layoutText(bold).width);
    bold.font="inter-fake-v1"; REQUIRE_FALSE(validText(bold));
}

TEST_CASE("Shared typography updates native instances after variants and preserves per-field overrides") {
    PropertyFixture f; auto definition=f.definition();
    definition.axes.push_back({"tone",{"normal","airy"},"airy"});
    NodePatch airy; airy.textStyle=TextStyle{}; airy.textStyle->letterSpacing=2;
    definition.rules={{{{"tone","airy"}},f.label,airy}};
    REQUIRE(f.editor.setComponentDefinition(f.main,definition));
    const auto id=*f.editor.addToken(TokenType::Typography);
    const auto beforeBinding=f.session.tokenRevision();
    NodePatch bind; bind.typography=id; REQUIRE(f.editor.setProps(f.label,bind));
    REQUIRE(f.session.tokenRevision() > beforeBinding);
    const auto afterBinding=f.session.tokenRevision(); NodePatch geometry; geometry.x=15; REQUIRE(f.editor.setProps(f.label,geometry));
    REQUIRE(f.session.tokenRevision() == afterBinding);
    REQUIRE_FALSE(f.session.edit(DeleteToken{id}));
    const auto copy=f.copy(f.first); NodePatch local; local.text=f.session.document().view(copy)->text; local.text->font="inter-bold-v1";
    REQUIRE(f.editor.setProps(copy,local));
    auto token=*f.session.document().findToken(id); auto& style=std::get<TextStyle>(*token.value);
    style.fontSize=36; style.font="inter-oblique-v1"; style.letterSpacing=1;
    REQUIRE(f.session.edit(ReplaceToken{token}));
    REQUIRE(f.session.document().view(f.label)->text.font == "inter-oblique-v1");
    REQUIRE(f.session.document().view(copy)->text.font == "inter-bold-v1");
    REQUIRE(f.session.document().view(copy)->text.fontSize == 36);
    REQUIRE(f.session.document().view(copy)->text.letterSpacing == 2);
    REQUIRE(f.content(f.first) == "Continuar");
    REQUIRE(f.session.document().find(f.label)->text.fontSize == 16);
    REQUIRE(f.session.document().box(copy).h == 36*1.4);
    const auto saved=snapshotToJson(f.session.document()); REQUIRE(saved.find("\"version\":11") != std::string::npos);
    const auto ops=snapshotFromJson(saved); REQUIRE(ops.has_value()); Document loaded; for(const auto& op:*ops) REQUIRE(loaded.apply(op));
    REQUIRE(loaded.view(copy)->text.font == "inter-bold-v1"); REQUIRE(loaded.view(copy)->text.fontSize == 36);
    auto old=saved; old.replace(old.find("\"version\":11"),12,"\"version\":10"); REQUIRE_FALSE(snapshotFromJson(old).has_value());
    REQUIRE(f.editor.undo()); REQUIRE(f.session.document().view(copy)->text.fontSize == 16);
    REQUIRE(f.editor.redo()); REQUIRE(f.session.document().view(copy)->text.fontSize == 36);
    REQUIRE(f.editor.resetOverrides(copy)); REQUIRE(f.session.document().view(copy)->text.font == "inter-oblique-v1");
}

TEST_CASE("Detaching shared typography preserves resolved metrics and undo restores the live binding") {
    PropertyFixture f; REQUIRE(f.editor.setComponentDefinition(f.main,f.definition()));
    const auto id=*f.editor.addToken(TokenType::Typography); auto token=*f.session.document().findToken(id);
    std::get<TextStyle>(*token.value).fontSize=40; std::get<TextStyle>(*token.value).font="inter-bold-oblique-v1";
    REQUIRE(f.session.edit(ReplaceToken{token})); NodePatch bind; bind.typography=id; REQUIRE(f.editor.setProps(f.label,bind));
    NodePatch detach; detach.typography=Id{}; detach.textStyle=std::get<TextStyle>(*token.value);
    REQUIRE(f.editor.setProps(f.label,detach));
    REQUIRE(f.session.document().view(f.label)->typography.isNil());
    REQUIRE(f.session.document().view(f.label)->text.fontSize == 40);
    REQUIRE(f.session.document().view(f.label)->text.font == "inter-bold-oblique-v1");
    REQUIRE(f.session.document().find(f.label)->text.content == "Texto base");
    REQUIRE(f.editor.undo()); REQUIRE(f.session.document().view(f.label)->typography == id);
    REQUIRE(f.session.document().find(f.label)->text.fontSize == 16);
    REQUIRE(f.editor.redo()); REQUIRE(f.session.document().view(f.label)->typography.isNil());
    REQUIRE(f.session.edit(DeleteToken{id})); REQUIRE(f.session.document().view(f.copy(f.first))->text.fontSize == 40);
}

TEST_CASE("Shared typography rejects invalid resources and binding changes in copied layers or rules") {
    PropertyFixture f; const auto id=*f.editor.addToken(TokenType::Typography);
    const auto original=*f.session.document().findToken(id);
    for (int invalid=0;invalid<5;++invalid) {
        auto token=original; auto& style=std::get<TextStyle>(*token.value);
        if (invalid==0) style.font.reset();
        if (invalid==1) style.fontSize=std::numeric_limits<double>::infinity();
        if (invalid==2) style.font="remote";
        if (invalid==3) token.modifiers.push_back(MagnitudeModifier{});
        if (invalid==4) {token.value.reset(); token.source[kDefaultModeId]=id;}
        REQUIRE_FALSE(f.session.edit(ReplaceToken{token}));
    }
    NodePatch bind; bind.typography=id;
    REQUIRE_FALSE(f.editor.setProps(f.main,bind)); REQUIRE_FALSE(f.editor.setProps(f.copy(f.first),bind));
    auto definition=f.definition(); definition.axes.push_back({"tone",{"a","b"},"a"});
    definition.rules={{{{"tone","b"}},f.label,bind}}; REQUIRE_FALSE(f.editor.setComponentDefinition(f.main,definition));
    Node node; node.id=f.session.newId(); node.type=NodeType::Instance; node.component=f.main; node.order="a9";
    node.overrides.push_back({f.label,bind}); REQUIRE_FALSE(f.session.edit(CreateNode{node}));
    const auto color=*f.editor.addToken(TokenType::Color); bind.typography=color; REQUIRE_FALSE(f.editor.setProps(f.label,bind));
    bind.typography=f.session.newId(); REQUIRE_FALSE(f.editor.setProps(f.label,bind));
    bind.typography=id; REQUIRE(f.editor.setProps(f.label,bind));
    NodePatch hidden; hidden.textStyle=TextStyle{}; hidden.textStyle->fontSize=28; REQUIRE_FALSE(f.session.edit(SetProps{f.label,hidden}));
    auto json=snapshotToJson(f.session.document()); REQUIRE(snapshotFromJson(json).has_value());
    REQUIRE_FALSE(tokenFromJson(R"({"id":"00000000-0000-0000-0000-000000000888","type":"typography","identifier":"bad","order":"a0","value":{"font":"remote","fontSize":20,"lineHeight":1.4,"letterSpacing":0,"align":"left"}})"));
}

TEST_CASE("Editing content of shared-style source text keeps its raw fallback and live metrics independent") {
    PropertyFixture f; const auto id=*f.editor.addToken(TokenType::Typography);
    auto token=*f.session.document().findToken(id); std::get<TextStyle>(*token.value).fontSize=32;
    REQUIRE(f.session.edit(ReplaceToken{token})); NodePatch bind; bind.typography=id; REQUIRE(f.editor.setProps(f.label,bind));
    NodePatch content; content.text=f.session.document().view(f.label)->text; content.text->content="Editar contenido";
    REQUIRE(f.editor.setProps(f.label,content)); REQUIRE(f.session.document().find(f.label)->text.fontSize == 16);
    REQUIRE(f.session.document().view(f.label)->text.fontSize == 32); REQUIRE(f.session.document().view(f.label)->text.content == "Editar contenido");
    REQUIRE(f.editor.undo()); REQUIRE(f.session.document().view(f.label)->text.content == "Texto base");
    REQUIRE(f.editor.redo()); REQUIRE(f.session.document().view(f.label)->text.content == "Editar contenido");
    auto unbound=*f.editor.addToken(TokenType::Typography); REQUIRE(f.session.edit(DeleteToken{unbound}));
}

TEST_CASE("Detaching an instance bakes differing typography and keeps matching shared styles live") {
    PropertyFixture f; REQUIRE(f.editor.setComponentDefinition(f.main,f.definition()));
    const auto id=*f.editor.addToken(TokenType::Typography); NodePatch bind; bind.typography=id; REQUIRE(f.editor.setProps(f.label,bind));
    NodePatch local; local.text=f.session.document().view(f.copy(f.first))->text; local.text->font="inter-bold-v1";
    REQUIRE(f.editor.setProps(f.copy(f.first),local));
    const auto frame=f.editor.detachInstance(f.first); REQUIRE(frame.has_value());
    const auto child=f.session.document().viewChildren(*frame).front();
    REQUIRE(f.session.document().view(child)->text.font == "inter-bold-v1");
    REQUIRE(f.session.document().view(child)->typography.isNil());
    REQUIRE(f.editor.undo()); REQUIRE(f.session.document().view(f.copy(f.first))->text.font == "inter-bold-v1");
    REQUIRE(f.editor.redo()); REQUIRE(f.session.document().view(child)->text.font == "inter-bold-v1");
    const auto second=f.editor.detachInstance(f.second); REQUIRE(second.has_value());
    const auto matching=f.session.document().viewChildren(*second).front();
    REQUIRE(f.session.document().view(matching)->typography == id);
    auto token=*f.session.document().findToken(id); std::get<TextStyle>(*token.value).fontSize=28;
    REQUIRE(f.session.edit(ReplaceToken{token})); REQUIRE(f.session.document().view(matching)->text.fontSize == 28);
    REQUIRE(f.session.document().view(child)->text.fontSize == 16);
}

TEST_CASE("Real oblique faces keep ink inside Hug bounds and account for overhang when wrapping") {
    for (const auto font : {"inter-oblique-v1", "inter-bold-oblique-v1"}) {
        TextRun text; text.font=font; text.fontSize=48;
        for (const auto content : {"f", "j", "Tj", "AV", "Diseño: José", "  f  ", "f\nj"}) {
            text.content=content; REQUIRE(validText(text));
            const auto result=layoutText(text);
            for (const auto& glyph : result.glyphs) {
                REQUIRE(glyph.box.x >= -1e-8);
                REQUIRE(glyph.box.x + glyph.box.w <= result.width + 1e-8);
            }
            text.align=TextAlign::Right;
            const auto aligned=layoutText(text,result.width+50);
            for (const auto& glyph : aligned.glyphs) REQUIRE(glyph.box.x + glyph.box.w <= result.width+50+1e-8);
            text.align=TextAlign::Left;
        }
        text.content="ff"; const auto full=layoutText(text);
        const auto wrapped=layoutText(text,full.width-.01,true);
        REQUIRE(wrapped.height == full.height*2);
        REQUIRE(layoutText(text,0,true).glyphs.size() == 2);
        auto upright=text; upright.font=font==std::string("inter-oblique-v1") ? "inter-regular-v1" : "inter-bold-v1";
        REQUIRE(full.glyphs.front().uv.y > layoutText(upright).glyphs.front().uv.y);
    }
}

TEST_CASE("Oblique bound overrides inherit metrics and require snapshot 10 even only in a variant rule") {
    PropertyFixture f; REQUIRE(f.editor.setComponentDefinition(f.main,f.definition()));
    const auto copy=f.copy(f.first);
    NodePatch local; local.text=f.session.document().view(copy)->text; local.text->font="inter-bold-oblique-v1";
    REQUIRE(f.editor.setProps(copy,local));
    NodePatch source; source.text=f.session.document().view(f.label)->text; source.text->fontSize=36; source.text->font="inter-oblique-v1";
    REQUIRE(f.editor.setProps(f.label,source));
    REQUIRE(f.session.document().view(copy)->text.fontSize == 36);
    REQUIRE(f.session.document().view(copy)->text.font == "inter-bold-oblique-v1");
    REQUIRE(f.content(f.first) == "Continuar");
    const auto saved=snapshotToJson(f.session.document()); REQUIRE(saved.find("\"version\":10") != std::string::npos);
    const auto ops=snapshotFromJson(saved); REQUIRE(ops.has_value()); Document loaded; for(const auto& op:*ops) REQUIRE(loaded.apply(op));
    REQUIRE(loaded.view(copy)->text.font == "inter-bold-oblique-v1");
    auto old=saved; old.replace(old.find("\"version\":10"),12,"\"version\":9"); REQUIRE_FALSE(snapshotFromJson(old).has_value());
    REQUIRE(f.editor.resetOverrides(copy)); REQUIRE(f.session.document().view(copy)->text.font == "inter-oblique-v1");
    REQUIRE(f.editor.undo()); REQUIRE(f.session.document().view(copy)->text.font == "inter-bold-oblique-v1");
    REQUIRE(f.editor.redo()); REQUIRE(f.session.document().view(copy)->text.font == "inter-oblique-v1");
    PropertyFixture variant; auto definition=variant.definition();
    definition.axes.push_back({"tone", {"normal", "emphasis"}, "normal"});
    NodePatch emphasis; emphasis.textStyle=TextStyle{}; emphasis.textStyle->font="inter-oblique-v1";
    definition.rules={{{{"tone","emphasis"}},variant.label,emphasis}};
    REQUIRE(variant.editor.setComponentDefinition(variant.main,definition));
    const auto dormant=snapshotToJson(variant.session.document());
    REQUIRE(dormant.find("\"version\":10") != std::string::npos);
    REQUIRE(snapshotFromJson(dormant).has_value());
    old=dormant; old.replace(old.find("\"version\":10"),12,"\"version\":9"); REQUIRE_FALSE(snapshotFromJson(old).has_value());
}

TEST_CASE("Bound font choices remain independent from content and inherited metrics in snapshot 9") {
    PropertyFixture f; REQUIRE(f.editor.setComponentDefinition(f.main,f.definition()));
    const auto copy=f.copy(f.first);
    NodePatch local; local.text=f.session.document().view(copy)->text; local.text->font="inter-bold-v1";
    REQUIRE(f.editor.setProps(copy,local));
    const auto& patch=f.session.document().find(f.first)->overrides.at(0).patch;
    REQUIRE(patch.textStyle->font == "inter-bold-v1"); REQUIRE_FALSE(patch.textStyle->fontSize.has_value()); REQUIRE_FALSE(patch.text.has_value());
    NodePatch source; source.text=f.session.document().view(f.label)->text; source.text->fontSize=32;
    REQUIRE(f.editor.setProps(f.label,source));
    REQUIRE(f.session.document().view(copy)->text.fontSize == 32);
    REQUIRE(f.session.document().view(copy)->text.font == "inter-bold-v1");
    local.text=f.session.document().view(copy)->text; local.text->font="inter-regular-v1";
    REQUIRE(f.editor.setProps(copy,local));
    source.text=f.session.document().view(f.label)->text; source.text->font="inter-bold-v1";
    REQUIRE(f.editor.setProps(f.label,source));
    REQUIRE(f.session.document().view(copy)->text.font == "inter-regular-v1");
    REQUIRE(f.session.document().view(f.copy(f.second))->text.font == "inter-bold-v1");
    REQUIRE(f.content(f.first) == "Continuar");
    const auto saved=snapshotToJson(f.session.document()); REQUIRE(saved.find("\"version\":9") != std::string::npos);
    const auto ops=snapshotFromJson(saved); REQUIRE(ops.has_value()); Document loaded; for(const auto& op:*ops) REQUIRE(loaded.apply(op));
    REQUIRE(loaded.view(copy)->text.font == "inter-regular-v1");
    auto old=saved; old.replace(old.find("\"version\":9"),11,"\"version\":8"); REQUIRE_FALSE(snapshotFromJson(old).has_value());
    REQUIRE(f.editor.resetOverrides(copy)); REQUIRE(f.session.document().view(copy)->text.font == "inter-bold-v1");
    REQUIRE(f.editor.undo()); REQUIRE(f.session.document().view(copy)->text.font == "inter-regular-v1");
    REQUIRE(f.editor.redo()); REQUIRE(f.session.document().view(copy)->text.font == "inter-bold-v1");
    REQUIRE_FALSE(patchFromJson(R"({"textStyle":{"font":"remote"}})").has_value());
}

TEST_CASE("Changing the real font face reflows wrapped text and restores the prior layout on undo") {
    Session session{IdGenerator{89}}; Camera camera; Editor editor{session,camera};
    Node text; text.id=session.newId(); text.type=NodeType::Text; text.order="a0"; text.text.content="Continuar con diseño";
    auto bold=text.text; bold.font="inter-bold-v1";
    text.w=(layoutText(text.text).width+layoutText(bold).width)/2; text.heightSizing=Sizing::Hug; text.textWrap=true;
    REQUIRE(session.edit(CreateNode{text})); const auto before=session.document().box(text.id).h;
    NodePatch patch; patch.text=bold; REQUIRE(editor.setProps(text.id,patch));
    REQUIRE(session.document().box(text.id).h > before);
    REQUIRE(editor.undo()); REQUIRE(session.document().box(text.id).h == before);
    REQUIRE(editor.redo()); REQUIRE(session.document().view(text.id)->text.font == "inter-bold-v1");
}

TEST_CASE("Bundled font measures accented Latin, kerning, alignment and explicit lines") {
    TextRun text;
    text.content = "AV";
    auto pair = layoutText(text);
    TextRun a = text; a.content = "A";
    TextRun v = text; v.content = "V";
    REQUIRE(pair.width < layoutText(a).width + layoutText(v).width);
    REQUIRE(pair.glyphs.size() == 2);
    text.content = "Dise\xc3\xb1o: Jos\xc3\xa9";
    REQUIRE(validText(text));
    const auto first = layoutText(text);
    text.content += "\nHola";
    const auto multiline = layoutText(text);
    REQUIRE_THAT(multiline.height, WithinAbs(first.height * 2, 1e-8));
    text.align = TextAlign::Center;
    auto centered = layoutText(text, multiline.width + 100);
    REQUIRE_THAT(centered.glyphs[0].box.x - multiline.glyphs[0].box.x, WithinAbs(50, 1e-8));
    text.letterSpacing = 2;
    REQUIRE(layoutText(text).width > multiline.width);
    const auto atlas = textAtlas();
    REQUIRE(atlas.size() == kTextAtlasSize * kTextAtlasSize);
    REQUIRE(std::any_of(atlas.begin(), atlas.end(), [](auto p) { return p != 0; }));
}

TEST_CASE("Text refuses unsupported scripts, malformed UTF-8 and untrusted fonts") {
    TextRun text;
    for (const auto& value : {std::string("\xc0\xaf"), std::string("\xed\xa0\x80"), std::string("\xf0\x9f\x98\x80"),
                              std::string("\xd8\xa7"), std::string("a\xcc\x81"), std::string("a\0b", 3), std::string(4097, 'a')}) {
        text.content = value;
        REQUIRE_FALSE(validText(text));
    }
    text = {}; text.font = "remote-font"; REQUIRE_FALSE(validText(text));
    text = {}; text.fontSize = std::numeric_limits<double>::infinity(); REQUIRE_FALSE(validText(text));
    text = {}; text.lineHeight = .5; REQUIRE_FALSE(validText(text));
}

TEST_CASE("Text is editable native anatomy, inherits and overrides with independent Hug layout") {
    Session session{IdGenerator{72}}; Camera camera; Editor editor{session, camera};
    Node main; main.id = session.newId(); main.type = NodeType::Component; main.order = "a0"; main.w = 200; main.h = 70;
    REQUIRE(session.edit(CreateNode{main}));
    Node text; text.id = session.newId(); text.parent = main.id; text.type = NodeType::Text; text.order = "a0";
    text.text.content = "Continuar"; text.widthSizing = text.heightSizing = Sizing::Hug;
    REQUIRE(session.edit(CreateNode{text}));
    auto instance = editor.createInstance(main.id); REQUIRE(instance);
    const auto copy = session.document().viewChildren(*instance).at(0);
    const double original = session.document().box(copy).w;
    NodePatch patch; patch.text = text.text; patch.text->content = "Confirmar y continuar";
    REQUIRE(editor.setProps(copy, patch));
    REQUIRE(session.document().view(copy)->text.content == patch.text->content);
    REQUIRE(session.document().find(text.id)->text.content == "Continuar");
    REQUIRE(session.document().box(copy).w > original);
    REQUIRE(editor.undo()); REQUIRE(session.document().view(copy)->text.content == "Continuar");
    REQUIRE(editor.redo());
    auto snapshot = snapshotToJson(session.document());
    REQUIRE(snapshot.find("\"version\":5") != std::string::npos);
    auto ops = snapshotFromJson(snapshot); REQUIRE(ops.has_value());
    Document restored; for (const auto& op : *ops) REQUIRE(restored.apply(op));
    REQUIRE(restored.view(restored.viewChildren(*instance).at(0))->text.content == patch.text->content);
    auto downgrade = snapshot; downgrade.replace(downgrade.find("\"version\":5"), 11, "\"version\":4");
    REQUIRE_FALSE(snapshotFromJson(downgrade).has_value());
    patch.text->font = "external"; REQUIRE_FALSE(editor.setProps(copy, patch));
    REQUIRE(editor.resetOverrides(copy));
    REQUIRE(session.document().view(copy)->text.content == "Continuar");
}

TEST_CASE("Text draw list emits only clipped glyphs with paint opacity, never a filled rectangle") {
    Document doc;
    Node text; text.id = {77,1}; text.type = NodeType::Text; text.order = "a0";
    text.text.content = "Hola"; text.w = 100; text.h = 40; text.opacity = .5;
    REQUIRE(doc.apply(CreateNode{text}));
    std::vector<DrawItem> items;
    REQUIRE(buildDrawList(doc, {-100,-100,1000,1000}, kDefaultModeId, items) == 1);
    REQUIRE(items.size() == 4);
    for (const auto& item : items) {
        REQUIRE(item.kind == DrawKind::Glyph);
        REQUIRE(item.a.rect.x >= 0); REQUIRE(item.a.rect.x + item.a.rect.w <= 1);
        REQUIRE_THAT(item.color.a, WithinAbs(.5, 1e-6));
        REQUIRE(item.clip.w == text.w);
    }
    NodePatch hide; hide.fillVisible = false; REQUIRE(doc.apply(SetProps{text.id, hide}));
    buildDrawList(doc, {-100,-100,1000,1000}, kDefaultModeId, items); REQUIRE(items.empty());
}

TEST_CASE("Text tool creates real text and frame-shaped nodes cannot carry hidden text") {
    Session session{IdGenerator{73}}; Camera camera; Editor editor{session, camera};
    editor.setTool(Tool::Text); editor.pointerDown({0,0}, PointerButton::Primary); editor.pointerUp();
    const auto* text = session.document().find(editor.selection().at(0)); REQUIRE(text);
    REQUIRE(text->type == NodeType::Text); REQUIRE(text->text.content == "Texto");
    REQUIRE(session.document().box(text->id).w > 0);
    REQUIRE(editor.undo()); REQUIRE(session.document().size() == 0);
    Node rect; rect.id = session.newId(); rect.order = "a0"; rect.text.content = "hidden";
    REQUIRE_FALSE(session.edit(CreateNode{rect}));
}

TEST_CASE("Text paint rejects unsupported decorations and non-text override addresses atomically") {
    Document doc;
    Node main; main.id = {78,1}; main.type = NodeType::Component; main.order = "a0";
    REQUIRE(doc.apply(CreateNode{main}));
    Node text; text.id = {78,2}; text.parent = main.id; text.type = NodeType::Text; text.order = "a0";
    text.text.content = "Texto"; REQUIRE(doc.apply(CreateNode{text}));
    Node instance; instance.id = {78,3}; instance.type = NodeType::Instance; instance.component = main.id; instance.order = "a1";
    REQUIRE(doc.apply(CreateNode{instance}));
    NodePatch radius; radius.cornerRadius = 4.0;
    const auto before = snapshotToJson(doc);
    REQUIRE_FALSE(doc.apply(SetProps{text.id, radius}));
    REQUIRE_FALSE(doc.apply(SetOverride{instance.id, text.id, radius}));
    NodePatch content; content.text = text.text;
    REQUIRE_FALSE(doc.apply(SetOverride{instance.id, main.id, content}));
    REQUIRE_FALSE(doc.apply(SetOverride{instance.id, Id{78,999}, content}));
    NodePatch style; style.textStyle = TextStyle{32, 1.4, 0, TextAlign::Left};
    REQUIRE_FALSE(doc.apply(SetOverride{instance.id, main.id, style}));
    REQUIRE_FALSE(doc.apply(SetProps{main.id, style}));
    style.text = text.text; REQUIRE_FALSE(doc.apply(SetProps{text.id, style}));
    REQUIRE(snapshotToJson(doc) == before);
}
