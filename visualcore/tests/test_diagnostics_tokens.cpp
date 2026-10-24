#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include "vc/diagnostics.hpp"
#include "vc/document.hpp"

using Catch::Matchers::ContainsSubstring;
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

Token color(std::uint64_t id, const char* identifier, Oklch value = {0.5, 0.1, 100}) {
    Token token;
    token.id = idOf(id);
    token.identifier = identifier;
    token.value = value;
    return token;
}

Token dimension(std::uint64_t id, const char* identifier) {
    Token token;
    token.id = idOf(id);
    token.type = TokenType::Dimension;
    token.identifier = identifier;
    token.value = Magnitude{4, Unit::Px};
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

vc::Diagnostic refusalOf(Document& doc, const vc::OpBody& op) {
    vc::clearDiagnostic();
    REQUIRE_FALSE(doc.apply(op));
    const auto diagnostic = vc::lastDiagnostic();
    REQUIRE(diagnostic.has_value());
    return *diagnostic;
}

const Id kDark{9, 9};

} // namespace

TEST_CASE("token refusals explain the rule that was broken") {
    Document doc;
    REQUIRE(doc.apply(CreateToken{color(1, "white")}));
    REQUIRE(doc.apply(CreateToken{dimension(2, "space-1")}));

    SECTION("an invalid identifier") {
        const auto why = refusalOf(doc, CreateToken{dimension(3, "space.2xl")});
        CHECK_THAT(why.message, ContainsSubstring("space.2xl"));
        CHECK_THAT(why.message, ContainsSubstring("kebab-case"));
    }
    SECTION("a duplicate identifier in the same family") {
        const auto why = refusalOf(doc, CreateToken{color(3, "white")});
        CHECK_THAT(why.message, ContainsSubstring("white"));
        CHECK_THAT(why.message, ContainsSubstring("already used"));
    }
    SECTION("an alias to a token of another family") {
        const auto why = refusalOf(doc, CreateToken{alias(3, TokenType::Color, "x", {{vc::kDefaultModeId, idOf(2)}})});
        CHECK_THAT(why.message, ContainsSubstring(idOf(2).toString()));
        CHECK_THAT(why.message, ContainsSubstring("dimension"));
        CHECK_THAT(why.message, ContainsSubstring("color"));
    }
    SECTION("an alias without a default mode source") {
        doc.apply(vc::CreateMode{{kDark, "Dark", "a1"}});
        const auto why = refusalOf(doc, CreateToken{alias(3, TokenType::Color, "x", {{kDark, idOf(1)}})});
        CHECK_THAT(why.message, ContainsSubstring("default mode"));
    }
    SECTION("units outside the family") {
        Token token = dimension(3, "x");
        token.value = Magnitude{4, Unit::Ms};
        const auto why = refusalOf(doc, CreateToken{token});
        CHECK_THAT(why.message, ContainsSubstring("px, rem, em or pt"));
    }
    SECTION("a typography token with negative letterSpacing") {
        Token token;
        token.id = idOf(3);
        token.type = TokenType::Typography;
        token.identifier = "body";
        vc::TextStyle style;
        style.font = "inter-regular-v1";
        style.fontSize = 16;
        style.lineHeight = 1.5;
        style.letterSpacing = -1;
        style.align = vc::TextAlign::Left;
        token.value = style;
        const auto why = refusalOf(doc, CreateToken{token});
        CHECK_THAT(why.message, ContainsSubstring("letterSpacing"));
    }
    SECTION("a shadow layer referencing a missing color") {
        Token token;
        token.id = idOf(3);
        token.type = TokenType::Shadow;
        token.identifier = "shadow-md";
        token.value = vc::ShadowValue{{vc::ShadowLayerValue{0, 4, 12, 0, false, idOf(99), {}}}};
        const auto why = refusalOf(doc, CreateToken{token});
        CHECK_THAT(why.path, ContainsSubstring("layers[0]"));
        CHECK_THAT(why.message, ContainsSubstring(idOf(99).toString()));
    }
    SECTION("an alias cycle") {
        doc.apply(CreateToken{alias(3, TokenType::Color, "linked", {{vc::kDefaultModeId, idOf(1)}})});
        const auto why = refusalOf(doc, vc::ReplaceToken{alias(1, TokenType::Color, "white", {{vc::kDefaultModeId, idOf(3)}})});
        CHECK_THAT(why.message, ContainsSubstring("cycle"));
    }
    SECTION("deleting a referenced token") {
        doc.apply(CreateToken{alias(3, TokenType::Color, "linked", {{vc::kDefaultModeId, idOf(1)}})});
        const auto why = refusalOf(doc, vc::DeleteToken{idOf(1)});
        CHECK_THAT(why.message, ContainsSubstring("referenced"));
    }
    SECTION("changing a token's family") {
        const auto why = refusalOf(doc, vc::ReplaceToken{dimension(1, "white")});
        CHECK_THAT(why.message, ContainsSubstring("family"));
    }
}

TEST_CASE("mode refusals explain the rule that was broken") {
    Document doc;

    SECTION("deleting the default mode") {
        const auto why = refusalOf(doc, vc::DeleteMode{vc::kDefaultModeId});
        CHECK_THAT(why.message, ContainsSubstring("default mode"));
    }
    SECTION("deleting a mode an alias names") {
        doc.apply(vc::CreateMode{{kDark, "Dark", "a1"}});
        doc.apply(CreateToken{color(1, "white")});
        doc.apply(CreateToken{alias(2, TokenType::Color, "surface", {{vc::kDefaultModeId, idOf(1)}, {kDark, idOf(1)}})});
        const auto why = refusalOf(doc, vc::DeleteMode{kDark});
        CHECK_THAT(why.message, ContainsSubstring("surface"));
    }
    SECTION("a mode without a name") {
        const auto why = refusalOf(doc, vc::CreateMode{{kDark, "", "a1"}});
        CHECK_THAT(why.message, ContainsSubstring("name"));
    }
}

TEST_CASE("a node referencing the wrong token family names the token and the family") {
    Document doc;
    REQUIRE(doc.apply(CreateToken{dimension(2, "space-1")}));
    vc::Node node;
    node.id = idOf(50);
    node.w = node.h = 10;
    node.fill = vc::TokenRef{idOf(2)};

    vc::clearDiagnostic();
    REQUIRE_FALSE(doc.apply(vc::CreateNode{node}));
    const auto why = vc::lastDiagnostic();
    REQUIRE(why.has_value());
    CHECK_THAT(why->message, ContainsSubstring(idOf(2).toString()));
    CHECK_THAT(why->message, ContainsSubstring("is a dimension"));
    CHECK_THAT(why->message, ContainsSubstring("color token"));
}
