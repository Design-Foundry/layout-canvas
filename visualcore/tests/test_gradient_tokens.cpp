#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "vc/diagnostics.hpp"
#include "vc/document.hpp"
#include "vc/editor.hpp"
#include "vc/json.hpp"
#include "vc/session.hpp"

using Catch::Matchers::WithinAbs;
using vc::CreateToken;
using vc::Document;
using vc::GradientKind;
using vc::GradientStopValue;
using vc::GradientValue;
using vc::Id;
using vc::Oklch;
using vc::Token;
using vc::TokenType;

namespace {

Id idOf(std::uint64_t n) { return {7, n}; }
const Id kDark{9, 9};

Token color(std::uint64_t id, const char* identifier, Oklch value) {
    Token token;
    token.id = idOf(id);
    token.identifier = identifier;
    token.value = value;
    return token;
}

Token gradient(std::uint64_t id, const char* identifier, GradientValue value) {
    Token token;
    token.id = idOf(id);
    token.type = TokenType::Gradient;
    token.identifier = identifier;
    vc::normalizeGradientGeometry(value);
    token.value = std::move(value);
    return token;
}

GradientValue linear(double angle = 90) {
    GradientValue g;
    g.angle = angle;
    g.stops = {{idOf(1), 0.0, {}}, {idOf(2), 1.0, {}}};
    return g;
}

Document withColors() {
    Document doc;
    doc.apply(vc::CreateMode{{kDark, "Dark", "a1"}});
    doc.apply(CreateToken{color(10, "light-a", {0.9, 0.05, 30})});
    doc.apply(CreateToken{color(11, "dark-a", {0.3, 0.05, 30})});
    Token a;
    a.id = idOf(1);
    a.identifier = "stop-a";
    a.source = {{vc::kDefaultModeId, idOf(10)}, {kDark, idOf(11)}};
    doc.apply(CreateToken{a});
    doc.apply(CreateToken{color(2, "stop-b", {0.5, 0.2, 250})});
    return doc;
}

} // namespace

TEST_CASE("a gradient token resolves its stops per mode") {
    Document doc = withColors();
    REQUIRE(doc.apply(CreateToken{gradient(20, "sky", linear())}));

    const auto light = doc.resolveGradient(idOf(20), vc::kDefaultModeId);
    REQUIRE(light.has_value());
    REQUIRE(light->geometry == GradientKind::Linear);
    REQUIRE(light->angle == 90.0);
    REQUIRE(light->stops.size() == 2);
    REQUIRE_THAT(light->stops[0].color.l, WithinAbs(0.9, 1e-9));
    REQUIRE(light->stops[1].position == 1.0);

    const auto dark = doc.resolveGradient(idOf(20), kDark);
    REQUIRE_THAT(dark->stops[0].color.l, WithinAbs(0.3, 1e-9));

    REQUIRE_FALSE(doc.resolveGradient(idOf(1), vc::kDefaultModeId).has_value());
    REQUIRE(doc.isTokenReferenced(idOf(2)));
    REQUIRE_FALSE(doc.apply(vc::DeleteToken{idOf(2)}));
}

TEST_CASE("gradient stop modifiers apply to the stop color") {
    Document doc = withColors();
    GradientValue g = linear();
    g.stops[1].colorModifiers = {vc::ColorChannelModifier{vc::ColorChannel::Alpha, vc::ColorOp::Set, 0.0, std::nullopt}};
    REQUIRE(doc.apply(CreateToken{gradient(20, "fade", g)}));
    const auto resolved = doc.resolveGradient(idOf(20), vc::kDefaultModeId);
    REQUIRE_THAT(resolved->stops[1].color.a, WithinAbs(0.0, 1e-9));
}

TEST_CASE("gradient geometry defaults depend on the kind and refuses fields that do not apply") {
    GradientValue radial;
    radial.geometry = GradientKind::Radial;
    vc::normalizeGradientGeometry(radial);
    REQUIRE_FALSE(radial.angle.has_value());
    REQUIRE(*radial.shape == vc::RadialShape::Ellipse);
    REQUIRE(*radial.extent == vc::RadialExtent::FarthestCorner);
    REQUIRE(*radial.centerX == 50.0);

    GradientValue conic;
    conic.geometry = GradientKind::Conic;
    vc::normalizeGradientGeometry(conic);
    REQUIRE(*conic.angle == 0.0);
    REQUIRE(*conic.centerY == 50.0);
    REQUIRE_FALSE(conic.shape.has_value());

    GradientValue bad = linear();
    bad.centerX = 10;
    bad.centerY = 10;
    REQUIRE(vc::gradientGeometryProblem(bad) != nullptr);
    bad = radial;
    bad.angle = 45;
    REQUIRE(vc::gradientGeometryProblem(bad) != nullptr);
    bad = conic;
    bad.shape = vc::RadialShape::Circle;
    REQUIRE(vc::gradientGeometryProblem(bad) != nullptr);
    bad = radial;
    bad.centerY.reset();
    REQUIRE(vc::gradientGeometryProblem(bad) != nullptr);
}

TEST_CASE("gradient tokens are validated as composites") {
    Document doc = withColors();

    GradientValue oneStop = linear();
    oneStop.stops.pop_back();
    REQUIRE_FALSE(doc.apply(CreateToken{gradient(20, "one", oneStop)}));

    GradientValue backwards = linear();
    backwards.stops[0].position = 0.8;
    backwards.stops[1].position = 0.2;
    REQUIRE(doc.apply(CreateToken{gradient(21, "backwards", backwards)}));

    GradientValue outside = linear();
    outside.stops[1].position = 1.4;
    REQUIRE_FALSE(doc.apply(CreateToken{gradient(22, "outside", outside)}));

    GradientValue notColor = linear();
    notColor.stops[0].color = idOf(99);
    REQUIRE_FALSE(doc.apply(CreateToken{gradient(23, "missing", notColor)}));

    Token aliased = gradient(24, "aliased", linear());
    aliased.value.reset();
    aliased.source = {{vc::kDefaultModeId, idOf(20)}};
    REQUIRE_FALSE(doc.apply(CreateToken{aliased}));

    REQUIRE(doc.apply(CreateToken{gradient(25, "ok", linear())}));
}

TEST_CASE("gradient tokens round-trip through the token and snapshot JSON") {
    Document doc = withColors();
    GradientValue g;
    g.geometry = GradientKind::Radial;
    g.repeating = true;
    g.shape = vc::RadialShape::Circle;
    g.extent = vc::RadialExtent::ClosestSide;
    g.centerX = 30;
    g.centerY = 70;
    g.stops = {{idOf(1), std::nullopt, {}}, {idOf(2), 0.4, {vc::ColorMixModifier{idOf(10), 0.5}}}, {idOf(2), std::nullopt, {}}};
    const Token token = gradient(20, "glow", g);
    REQUIRE(doc.apply(CreateToken{token}));

    const std::string snapshot = vc::snapshotToJson(doc);
    REQUIRE(snapshot.find(R"("version":28)") != std::string::npos);
    const auto ops = vc::snapshotFromJson(snapshot);
    REQUIRE(ops.has_value());
    Document restored;
    for (const vc::OpBody& op : *ops) REQUIRE(restored.apply(op));
    REQUIRE(restored.findToken(idOf(20))->value == token.value);

    const std::string rows = vc::tokensToJson(doc, kDark);
    REQUIRE(rows.find(R"("geometry":"radial")") != std::string::npos);
    REQUIRE(rows.find(R"("repeating":true)") != std::string::npos);
}

TEST_CASE("gradient token JSON refuses geometry that does not apply to the kind") {
    const std::string base = R"({"id":"00000000-0000-0000-0000-000000000007","type":"gradient","identifier":"g","order":"a",)";
    const std::string stops = R"("stops":[{"color":"00000000-0000-0000-0000-000000000007","position":0},{"color":"00000000-0000-0000-0000-000000000007","position":1}]})";
    REQUIRE(vc::tokenFromJson(base + R"("value":{"geometry":"linear","angle":45,)" + stops + "}").has_value());
    vc::clearDiagnostic();
    REQUIRE_FALSE(vc::tokenFromJson(base + R"("value":{"geometry":"linear","shape":"circle",)" + stops + "}").has_value());
    REQUIRE_FALSE(vc::tokenFromJson(base + R"("value":{"geometry":"radial","angle":10,)" + stops + "}").has_value());
    REQUIRE_FALSE(vc::tokenFromJson(base + R"("value":{"geometry":"mesh",)" + stops + "}").has_value());
    REQUIRE_FALSE(vc::tokenFromJson(base + R"("value":{"geometry":"conic","center":[10],)" + stops + "}").has_value());
}

TEST_CASE("snapshots before version 28 refuse gradient tokens") {
    const std::string snapshot = R"({"version":27,"modes":[],"nodes":[],"tokens":[{"id":"00000000-0000-0000-0000-000000000007","type":"gradient","identifier":"g","order":"a","value":{"geometry":"linear","stops":[]}}]})";
    vc::clearDiagnostic();
    REQUIRE_FALSE(vc::snapshotFromJson(snapshot).has_value());
}

TEST_CASE("the editor adds a gradient token once there is a color token") {
    vc::Session session;
    vc::Camera camera;
    vc::Editor editor{session, camera};
    REQUIRE_FALSE(editor.addToken(TokenType::Gradient).has_value());
    REQUIRE(editor.addToken(TokenType::Color).has_value());
    const auto id = editor.addToken(TokenType::Gradient);
    REQUIRE(id.has_value());
    const Token* token = session.document().findToken(*id);
    REQUIRE(token->identifier == "gradient-1");
    REQUIRE(session.document().resolveGradient(*id, vc::kDefaultModeId).has_value());
}

TEST_CASE("omitted stop positions are completed on resolve and the token keeps them omitted") {
    Document doc = withColors();
    GradientValue g = linear();
    g.stops = {{idOf(1), std::nullopt, {}}, {idOf(2), std::nullopt, {}}, {idOf(2), 0.5, {}},
               {idOf(1), std::nullopt, {}}, {idOf(2), std::nullopt, {}}, {idOf(1), std::nullopt, {}}};
    REQUIRE(doc.apply(CreateToken{gradient(20, "spread", g)}));
    REQUIRE_FALSE(std::get<GradientValue>(*doc.findToken(idOf(20))->value).stops[1].position.has_value());

    const auto resolved = doc.resolveGradient(idOf(20), vc::kDefaultModeId);
    REQUIRE(resolved->stops.size() == 6);
    const double expected[] = {0.0, 0.25, 0.5, 0.6666666666666666, 0.8333333333333334, 1.0};
    for (std::size_t i = 0; i < 6; ++i) REQUIRE_THAT(resolved->stops[i].position, WithinAbs(expected[i], 1e-9));
}

TEST_CASE("css stop order raises a lower position to the one before it") {
    std::vector<double> positions{0.0, 0.6, 0.3, 0.3, 1.0};
    vc::applyCssStopOrder(positions);
    REQUIRE(positions == std::vector<double>{0.0, 0.6, 0.6, 0.6, 1.0});
}

TEST_CASE("the center is a two number array and the field is called geometry") {
    const std::string base = R"({"id":"00000000-0000-0000-0000-000000000007","type":"gradient","identifier":"g","order":"a",)";
    const std::string stops = R"("stops":[{"color":"00000000-0000-0000-0000-000000000007"},{"color":"00000000-0000-0000-0000-000000000007"}]})";
    REQUIRE(vc::tokenFromJson(base + R"("value":{"geometry":"radial","center":[30,70],)" + stops + "}").has_value());
    REQUIRE_FALSE(vc::tokenFromJson(base + R"("value":{"geometry":"radial","center":[30],)" + stops + "}").has_value());
    REQUIRE_FALSE(vc::tokenFromJson(base + R"("value":{"kind":"radial",)" + stops + "}").has_value());
}
