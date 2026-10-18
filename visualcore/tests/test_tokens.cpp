#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "vc/document.hpp"
#include "vc/editor.hpp"
#include "vc/json.hpp"
#include "vc/session.hpp"

using Catch::Matchers::WithinAbs;
using vc::CreateToken;
using vc::Document;
using vc::Id;
using vc::Magnitude;
using vc::Oklch;
using vc::Token;
using vc::TokenType;
using vc::Unit;

namespace {

Id idOf(std::uint64_t n) {
    return {7, n};
}

Token color(std::uint64_t id, const char* identifier, Oklch value) {
    Token token;
    token.id = idOf(id);
    token.identifier = identifier;
    token.value = value;
    return token;
}

Token magnitude(std::uint64_t id, TokenType type, const char* identifier, double value, Unit unit) {
    Token token;
    token.id = idOf(id);
    token.type = type;
    token.identifier = identifier;
    token.value = Magnitude{value, unit};
    return token;
}

Token alias(std::uint64_t id, TokenType type, const char* identifier, std::map<Id, Id> source) {
    Token token;
    token.id = idOf(id);
    token.type = type;
    token.identifier = identifier;
    token.source = std::move(source);
    return token;
}

Oklch resolvedColor(const Document& doc, std::uint64_t id, const Id& mode = vc::kDefaultModeId) {
    return std::get<Oklch>(*doc.resolveToken(idOf(id), mode));
}

double resolvedNumber(const Document& doc, std::uint64_t id, const Id& mode = vc::kDefaultModeId) {
    return std::get<Magnitude>(*doc.resolveToken(idOf(id), mode)).value;
}

const Id kDark{9, 9};

} // namespace

TEST_CASE("identifiers follow Design Foundry's pattern") {
    REQUIRE(vc::isValidIdentifier("pine-50"));
    REQUIRE(vc::isValidIdentifier("surface.glass-strong"));
    REQUIRE(vc::isValidIdentifier("spacing-3"));
    REQUIRE_FALSE(vc::isValidIdentifier("Pine"));
    REQUIRE_FALSE(vc::isValidIdentifier("pine--50"));
    REQUIRE_FALSE(vc::isValidIdentifier("pine-"));
    REQUIRE_FALSE(vc::isValidIdentifier("3d"));
    REQUIRE_FALSE(vc::isValidIdentifier("a..b"));
    REQUIRE_FALSE(vc::isValidIdentifier(""));
}

TEST_CASE("primitive colors resolve opaque, whatever alpha the literal carried") {
    Document doc;
    REQUIRE(doc.apply(CreateToken{color(1, "pine-50", {0.6, 0.1, 160, 0.3})}));
    REQUIRE(resolvedColor(doc, 1).a == 1.0);
}

TEST_CASE("aliases resolve per mode and fall back to the default source") {
    Document doc;
    doc.apply(vc::CreateMode{{kDark, "Dark", "a1"}});
    doc.apply(CreateToken{color(1, "white", {1, 0, 0})});
    doc.apply(CreateToken{color(2, "black", {0, 0, 0})});
    REQUIRE(doc.apply(CreateToken{alias(3, TokenType::Color, "surface", {{vc::kDefaultModeId, idOf(1)}, {kDark, idOf(2)}})}));
    REQUIRE(doc.apply(CreateToken{alias(4, TokenType::Color, "text", {{vc::kDefaultModeId, idOf(2)}})}));

    REQUIRE(resolvedColor(doc, 3).l == 1.0);
    REQUIRE(resolvedColor(doc, 3, kDark).l == 0.0);
    REQUIRE(resolvedColor(doc, 4, kDark).l == 0.0);
    REQUIRE(resolvedColor(doc, 3, Id{1, 1}).l == 1.0);
}

TEST_CASE("invalid tokens are rejected") {
    Document doc;
    doc.apply(CreateToken{color(1, "white", {1, 0, 0})});
    doc.apply(CreateToken{magnitude(2, TokenType::Dimension, "space-1", 4, Unit::Px)});

    SECTION("an alias must name the default mode") {
        REQUIRE_FALSE(doc.apply(CreateToken{alias(3, TokenType::Color, "x", {{kDark, idOf(1)}})}));
    }
    SECTION("an alias must stay in its family") {
        REQUIRE_FALSE(doc.apply(CreateToken{alias(3, TokenType::Color, "x", {{vc::kDefaultModeId, idOf(2)}})}));
    }
    SECTION("identifiers are unique within a family") {
        REQUIRE_FALSE(doc.apply(CreateToken{color(3, "white", {0, 0, 0})}));
        REQUIRE(doc.apply(CreateToken{magnitude(3, TokenType::Opacity, "white", 0.5, Unit::None)}));
    }
    SECTION("units must fit the family") {
        REQUIRE_FALSE(doc.apply(CreateToken{magnitude(3, TokenType::Dimension, "x", 4, Unit::Ms)}));
        REQUIRE_FALSE(doc.apply(CreateToken{magnitude(3, TokenType::Duration, "x", 4, Unit::Px)}));
        REQUIRE(doc.apply(CreateToken{magnitude(3, TokenType::Duration, "x", 200, Unit::Ms)}));
    }
    SECTION("opacity stays within 0 and 1") {
        REQUIRE_FALSE(doc.apply(CreateToken{magnitude(3, TokenType::Opacity, "x", 1.5, Unit::None)}));
    }
    SECTION("a token is a primitive or an alias, not both or neither") {
        Token both = color(3, "x", {1, 0, 0});
        both.source = {{vc::kDefaultModeId, idOf(1)}};
        REQUIRE_FALSE(doc.apply(CreateToken{both}));
        Token neither = color(3, "x", {1, 0, 0});
        neither.value.reset();
        REQUIRE_FALSE(doc.apply(CreateToken{neither}));
    }
    SECTION("the family can't change") {
        Token changed = color(2, "space-1", {1, 0, 0});
        REQUIRE_FALSE(doc.apply(vc::ReplaceToken{changed}));
    }
}

TEST_CASE("alias cycles are rejected at write time") {
    Document doc;
    doc.apply(CreateToken{color(1, "a", {1, 0, 0})});
    doc.apply(CreateToken{alias(2, TokenType::Color, "b", {{vc::kDefaultModeId, idOf(1)}})});
    doc.apply(CreateToken{alias(3, TokenType::Color, "c", {{vc::kDefaultModeId, idOf(2)}})});

    REQUIRE_FALSE(doc.apply(vc::ReplaceToken{alias(1, TokenType::Color, "a", {{vc::kDefaultModeId, idOf(3)}})}));
    REQUIRE_FALSE(doc.apply(vc::ReplaceToken{alias(2, TokenType::Color, "b", {{vc::kDefaultModeId, idOf(2)}})}));
    REQUIRE(resolvedColor(doc, 3).l == 1.0);
}

TEST_CASE("color modifiers apply in order after resolution") {
    Document doc;
    doc.apply(CreateToken{color(1, "pine", {0.6, 0.1, 160})});
    doc.apply(CreateToken{color(2, "white", {1, 0, 0})});
    doc.apply(CreateToken{magnitude(3, TokenType::Opacity, "opacity-medium", 0.4, Unit::None)});

    Token lighter = alias(4, TokenType::Color, "pine-light", {{vc::kDefaultModeId, idOf(1)}});
    lighter.modifiers = {vc::ColorChannelModifier{vc::ColorChannel::L, vc::ColorOp::Delta, 0.2, std::nullopt},
                         vc::ColorChannelModifier{vc::ColorChannel::Alpha, vc::ColorOp::Set, std::nullopt, idOf(3)}};
    REQUIRE(doc.apply(CreateToken{lighter}));
    const Oklch light = resolvedColor(doc, 4);
    REQUIRE_THAT(light.l, WithinAbs(0.8, 1e-9));
    REQUIRE_THAT(light.a, WithinAbs(0.4, 1e-9));

    Token mixed = color(5, "pine-mist", {0.6, 0.1, 160});
    mixed.modifiers = {vc::ColorMixModifier{idOf(2), 0.5}};
    REQUIRE(doc.apply(CreateToken{mixed}));
    const Oklch mist = resolvedColor(doc, 5);
    REQUIRE_THAT(mist.l, WithinAbs(0.8, 1e-9));
    REQUIRE_THAT(mist.c, WithinAbs(0.05, 1e-9));
    REQUIRE_THAT(mist.h, WithinAbs(160, 1e-9));
}

TEST_CASE("modifier operands are type-checked") {
    Document doc;
    doc.apply(CreateToken{color(1, "pine", {0.6, 0.1, 160})});
    doc.apply(CreateToken{magnitude(2, TokenType::Dimension, "space-1", 4, Unit::Px)});

    Token badAlphaRef = color(3, "x", {0.5, 0, 0});
    badAlphaRef.modifiers = {vc::ColorChannelModifier{vc::ColorChannel::Alpha, vc::ColorOp::Set, std::nullopt, idOf(2)}};
    REQUIRE_FALSE(doc.apply(CreateToken{badAlphaRef}));

    Token refOnLightness = color(3, "x", {0.5, 0, 0});
    refOnLightness.modifiers = {vc::ColorChannelModifier{vc::ColorChannel::L, vc::ColorOp::Set, std::nullopt, idOf(1)}};
    REQUIRE_FALSE(doc.apply(CreateToken{refOnLightness}));

    Token colorStepOnLength = magnitude(3, TokenType::Dimension, "x", 4, Unit::Px);
    colorStepOnLength.modifiers = {vc::ColorMixModifier{idOf(1), 0.5}};
    REQUIRE_FALSE(doc.apply(CreateToken{colorStepOnLength}));
}

TEST_CASE("magnitude modifiers scale, offset and clamp") {
    Document doc;
    doc.apply(CreateToken{magnitude(1, TokenType::Dimension, "space-1", 4, Unit::Px)});
    doc.apply(CreateToken{magnitude(2, TokenType::Dimension, "space-cap", 10, Unit::Px)});

    Token step = alias(3, TokenType::Dimension, "space-3", {{vc::kDefaultModeId, idOf(1)}});
    step.modifiers = {vc::MagnitudeModifier{vc::MagnitudeOp::Factor, 3.0, std::nullopt},
                      vc::MagnitudeModifier{vc::MagnitudeOp::Delta, std::nullopt, idOf(1)},
                      vc::MagnitudeModifier{vc::MagnitudeOp::Min, std::nullopt, idOf(2)}};
    REQUIRE(doc.apply(CreateToken{step}));
    REQUIRE(resolvedNumber(doc, 3) == 10.0);
}

TEST_CASE("lengths convert to canvas pixels") {
    Document doc;
    doc.apply(CreateToken{magnitude(1, TokenType::Dimension, "radius-md", 0.5, Unit::Rem)});
    doc.apply(CreateToken{magnitude(2, TokenType::Dimension, "hairline", 0.75, Unit::Pt)});
    doc.apply(CreateToken{magnitude(3, TokenType::Ratio, "golden", 1.6, Unit::None)});

    REQUIRE(*doc.resolveLength(vc::TokenRef{idOf(1)}, vc::kDefaultModeId) == 8.0);
    REQUIRE_THAT(*doc.resolveLength(vc::TokenRef{idOf(2)}, vc::kDefaultModeId), WithinAbs(1.0, 1e-12));
    REQUIRE_FALSE(doc.resolveLength(vc::TokenRef{idOf(3)}, vc::kDefaultModeId).has_value());
    REQUIRE(*doc.resolveLength(12.0, vc::kDefaultModeId) == 12.0);
}

TEST_CASE("nodes may only reference tokens of the right family") {
    Document doc;
    doc.apply(CreateToken{color(1, "pine", {0.6, 0.1, 160})});
    doc.apply(CreateToken{magnitude(2, TokenType::Dimension, "radius-md", 8, Unit::Px)});
    vc::Node node;
    node.id = idOf(50);
    node.w = node.h = 10;
    node.fill = vc::TokenRef{idOf(2)};
    REQUIRE_FALSE(doc.apply(vc::CreateNode{node}));

    node.fill = vc::TokenRef{idOf(1)};
    node.cornerRadius = vc::TokenRef{idOf(2)};
    REQUIRE(doc.apply(vc::CreateNode{node}));
}

TEST_CASE("referenced tokens can't be deleted") {
    Document doc;
    doc.apply(CreateToken{color(1, "pine", {0.6, 0.1, 160})});
    doc.apply(CreateToken{alias(2, TokenType::Color, "brand", {{vc::kDefaultModeId, idOf(1)}})});
    doc.apply(CreateToken{color(3, "unused", {0.2, 0, 0})});

    REQUIRE(doc.isTokenReferenced(idOf(1)));
    REQUIRE_FALSE(doc.apply(vc::DeleteToken{idOf(1)}));

    const auto inverse = doc.apply(vc::DeleteToken{idOf(3)});
    REQUIRE(inverse.has_value());
    REQUIRE_FALSE(doc.findToken(idOf(3)));
    REQUIRE(doc.apply(*inverse));
    REQUIRE(doc.findToken(idOf(3))->identifier == "unused");
}

TEST_CASE("the default mode is permanent; others go once no alias names them") {
    Document doc;
    REQUIRE(doc.modes().size() == 1);
    REQUIRE_FALSE(doc.apply(vc::DeleteMode{vc::kDefaultModeId}));

    doc.apply(vc::CreateMode{{kDark, "Dark", "a1"}});
    doc.apply(CreateToken{color(1, "white", {1, 0, 0})});
    doc.apply(CreateToken{alias(2, TokenType::Color, "surface", {{vc::kDefaultModeId, idOf(1)}, {kDark, idOf(1)}})});
    REQUIRE_FALSE(doc.apply(vc::DeleteMode{kDark}));
    REQUIRE(doc.modes()[0]->id == vc::kDefaultModeId);
}

TEST_CASE("deleting a mode through the session strips it from aliases as one undo step") {
    vc::Session session;
    session.edit(vc::CreateMode{{kDark, "Dark", "a1"}});
    session.edit(CreateToken{color(1, "white", {1, 0, 0})});
    session.edit(CreateToken{color(2, "black", {0, 0, 0})});
    session.edit(CreateToken{alias(3, TokenType::Color, "surface", {{vc::kDefaultModeId, idOf(1)}, {kDark, idOf(2)}})});

    REQUIRE(session.deleteMode(kDark));
    REQUIRE(session.document().modes().size() == 1);
    REQUIRE(session.document().findToken(idOf(3))->source.size() == 1);

    session.undo();
    REQUIRE(session.document().findMode(kDark));
    REQUIRE(session.document().findToken(idOf(3))->source.at(kDark) == idOf(2));
}

TEST_CASE("token edits bump the token revision, not the structure revision") {
    vc::Session session;
    const auto structure = session.structureRevision();
    const auto tokens = session.tokenRevision();
    session.edit(CreateToken{color(1, "white", {1, 0, 0})});

    REQUIRE(session.tokenRevision() != tokens);
    REQUIRE(session.structureRevision() == structure);
}

TEST_CASE("token and mode ops survive a json round trip") {
    Token rich = alias(4, TokenType::Color, "brand.primary", {{vc::kDefaultModeId, idOf(1)}, {kDark, idOf(2)}});
    rich.displayName = "Brand";
    rich.description = "Main accent";
    rich.deprecated = true;
    rich.order = "a3";
    rich.modifiers = {vc::ColorChannelModifier{vc::ColorChannel::Alpha, vc::ColorOp::Set, std::nullopt, idOf(3)},
                      vc::ColorMixModifier{idOf(2), 0.25},
                      vc::ColorChannelModifier{vc::ColorChannel::H, vc::ColorOp::Delta, 10.0, std::nullopt}};
    Token length = magnitude(5, TokenType::Dimension, "space-2", 0.5, Unit::Rem);
    length.modifiers = {vc::MagnitudeModifier{vc::MagnitudeOp::Max, std::nullopt, idOf(6)}};

    const vc::OpBody ops[] = {
        CreateToken{rich},
        vc::ReplaceToken{length},
        CreateToken{magnitude(6, TokenType::Ratio, "golden", 1.618, Unit::None)},
        vc::DeleteToken{idOf(4)},
        vc::CreateMode{{kDark, "Dark", "a1"}},
        vc::ReplaceMode{{kDark, "Night", "a2"}},
        vc::DeleteMode{kDark},
    };
    for (const vc::OpBody& op : ops) {
        const auto parsed = vc::opFromJson(vc::opKind(op), vc::opToJson(op));
        REQUIRE(parsed.has_value());
        REQUIRE(*parsed == op);
    }
}

TEST_CASE("the editor adds tokens with free identifiers, last in their family") {
    vc::Session session;
    vc::Camera camera;
    vc::Editor editor{session, camera};

    const auto first = editor.addToken(TokenType::Color);
    const auto second = editor.addToken(TokenType::Color);
    const auto length = editor.addToken(TokenType::Dimension);
    REQUIRE(first.has_value());
    REQUIRE(session.document().findToken(*first)->identifier == "color-1");
    REQUIRE(session.document().findToken(*second)->identifier == "color-2");
    REQUIRE(session.document().findToken(*length)->identifier == "dimension-1");
    REQUIRE(session.document().findToken(*first)->order < session.document().findToken(*second)->order);

    const auto mode = editor.addMode();
    REQUIRE(session.document().findMode(*mode)->name == "Mode 2");
    editor.setActiveMode(*mode);
    REQUIRE(editor.deleteMode(*mode));
    REQUIRE(editor.activeMode() == vc::kDefaultModeId);
}

TEST_CASE("border and shadow tokens reference other tokens and resolve for strokes and shadows") {
    Document doc;
    const Id dark{9, 9};
    doc.apply(vc::CreateMode{{dark, "Dark", "a1"}});
    doc.apply(CreateToken{magnitude(1, TokenType::Dimension, "hairline", 1, Unit::Px)});
    doc.apply(CreateToken{color(2, "gray-200", {0.9, 0, 0})});
    doc.apply(CreateToken{color(3, "gray-800", {0.3, 0, 0})});
    doc.apply(CreateToken{alias(4, TokenType::Color, "border", {{vc::kDefaultModeId, idOf(2)}, {dark, idOf(3)}})});
    doc.apply(CreateToken{magnitude(5, TokenType::Opacity, "shade", 0.2, Unit::None)});

    Token border;
    border.id = idOf(10);
    border.type = TokenType::Border;
    border.identifier = "border-subtle";
    border.value = vc::BorderValue{idOf(1), vc::StrokeStyle::Solid, idOf(4)};
    REQUIRE(doc.apply(CreateToken{border}));

    Token shadow;
    shadow.id = idOf(11);
    shadow.type = TokenType::Shadow;
    shadow.identifier = "shadow-md";
    shadow.value = vc::ShadowValue{{vc::ShadowLayerValue{0, 4, 12, 0, false, idOf(3),
        {vc::ColorChannelModifier{vc::ColorChannel::Alpha, vc::ColorOp::Set, std::nullopt, idOf(5)}}}}};
    REQUIRE(doc.apply(CreateToken{shadow}));

    const auto stroke = doc.resolveStroke(vc::TokenRef{idOf(10)}, dark);
    REQUIRE(stroke.has_value());
    REQUIRE(stroke->widths[vc::kTop] == 1.0);
    REQUIRE(stroke->color.l == 0.3);

    const auto layers = doc.resolveShadows(vc::TokenRef{idOf(11)}, vc::kDefaultModeId);
    REQUIRE(layers.size() == 1);
    REQUIRE(layers[0].blur == 12);
    REQUIRE_THAT(layers[0].color.a, WithinAbs(0.2, 1e-9));

    REQUIRE(doc.isTokenReferenced(idOf(1)));
    REQUIRE_FALSE(doc.apply(vc::DeleteToken{idOf(4)}));

    Token badBorder = border;
    badBorder.id = idOf(12);
    badBorder.identifier = "bad";
    badBorder.value = vc::BorderValue{idOf(2), vc::StrokeStyle::Solid, idOf(4)};
    REQUIRE_FALSE(doc.apply(CreateToken{badBorder}));

    Token aliasedBorder = border;
    aliasedBorder.id = idOf(13);
    aliasedBorder.identifier = "aliased";
    aliasedBorder.value.reset();
    aliasedBorder.source = {{vc::kDefaultModeId, idOf(10)}};
    REQUIRE_FALSE(doc.apply(CreateToken{aliasedBorder}));
}

TEST_CASE("node strokes resolve per side and nothing when invisible") {
    Document doc;
    vc::Stroke stroke;
    stroke.width = 2.0;
    stroke.sides[vc::kBottom] = 6.0;
    const auto resolved = doc.resolveStroke(stroke, vc::kDefaultModeId);
    REQUIRE(resolved->widths == std::array<double, 4>{2, 2, 6, 2});

    stroke.style = vc::StrokeStyle::None;
    REQUIRE_FALSE(doc.resolveStroke(stroke, vc::kDefaultModeId).has_value());
    REQUIRE_FALSE(doc.resolveStroke(vc::Stroke{}, vc::kDefaultModeId).has_value());
}

TEST_CASE("node token references are checked for every bindable property") {
    Document doc;
    doc.apply(CreateToken{color(1, "pine", {0.6, 0.1, 160})});
    doc.apply(CreateToken{magnitude(2, TokenType::Opacity, "half", 0.5, Unit::None)});
    vc::Node node;
    node.id = idOf(50);
    node.w = node.h = 10;
    node.opacity = vc::TokenRef{idOf(1)};
    REQUIRE_FALSE(doc.apply(vc::CreateNode{node}));
    node.opacity = vc::TokenRef{idOf(2)};
    node.layout.gap = vc::TokenRef{idOf(1)};
    REQUIRE_FALSE(doc.apply(vc::CreateNode{node}));
    node.layout.gap = 0.0;
    REQUIRE(doc.apply(vc::CreateNode{node}));
    REQUIRE(doc.resolveOpacity(node.opacity, vc::kDefaultModeId) == 0.5);
    REQUIRE(doc.isTokenReferenced(idOf(2)));
}
