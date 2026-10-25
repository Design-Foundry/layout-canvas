#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <string>
#include "vc/diagnostics.hpp"
#include "vc/json.hpp"

using Catch::Matchers::ContainsSubstring;

TEST_CASE("prototype options retain structured diagnostics after the upstream merge", "[diagnostics][prototype]") {
    vc::clearDiagnostic();
    REQUIRE_FALSE(vc::patchFromJson(R"({"interaction":{"trigger":"click","action":"navigate","target":"0192a4b1-7c3d-4e5f-8a6b-000000000002","transition":"dissolve","durationMs":0}})").has_value());
    REQUIRE(vc::lastDiagnostic()->path == "interaction.durationMs");
    REQUIRE_THAT(vc::lastDiagnostic()->message,ContainsSubstring("1 to 2000"));
    vc::clearDiagnostic();
    REQUIRE_FALSE(vc::patchFromJson(R"({"interaction":{"trigger":"hover","action":"back"}})").has_value());
    REQUIRE(vc::lastDiagnostic()->path == "interaction.trigger");
    vc::clearDiagnostic();
    REQUIRE_FALSE(vc::snapshotFromJson(R"({"version":16,"modes":[],"tokens":[],"nodes":[{"id":"0192a4b1-7c3d-4e5f-8a6b-000000000002","type":"frame","order":"a0","interaction":{"trigger":"click","action":"close-overlay"}}]})").has_value());
    REQUIRE(vc::lastDiagnostic()->path == "nodes[0]");
    REQUIRE_THAT(vc::lastDiagnostic()->message,ContainsSubstring("17"));
}

namespace {

constexpr const char* kId = "0192a4b1-7c3d-4e5f-8a6b-000000000002";

std::string snapshot(int version, const std::string& nodes, const std::string& tokens = "[]") {
    return R"({"version":)" + std::to_string(version) + R"(,"modes":[],"tokens":)" + tokens + R"(,"nodes":)" + nodes + "}";
}

std::string node(const std::string& type, const std::string& extra = "") {
    return std::string{R"({"id":")"} + kId + R"(","type":")" + type + R"(","order":"a0")" + extra + "}";
}

std::string textNode(const std::string& fontSize, const std::string& font = "inter-regular-v1") {
    return node("text", R"(,"text":{"content":"a","font":")" + font + R"(","fontSize":)" + fontSize +
                            R"(,"lineHeight":1.2,"letterSpacing":0,"align":"left"})");
}

} // namespace

TEST_CASE("a patch field of the wrong type names the field", "[diagnostics][json]") {
    vc::clearDiagnostic();
    REQUIRE_FALSE(vc::patchFromJson(R"({"x":"4"})").has_value());
    const auto why = vc::lastDiagnostic();
    REQUIRE(why.has_value());
    REQUIRE(why->path == "x");
    REQUIRE(why->message == "must be a number");
}

TEST_CASE("a patch enum names its allowed values", "[diagnostics][json]") {
    vc::clearDiagnostic();
    REQUIRE_FALSE(vc::patchFromJson(R"({"widthSizing":"wide"})").has_value());
    REQUIRE(vc::lastDiagnostic() == vc::Diagnostic{"widthSizing", "sizing must be fixed, hug or fill"});

    vc::clearDiagnostic();
    REQUIRE_FALSE(vc::patchFromJson(R"({"layout":{"justify":"left"}})").has_value());
    const auto why = vc::lastDiagnostic();
    REQUIRE(why.has_value());
    REQUIRE(why->path == "layout.justify");
    REQUIRE_THAT(why->message, ContainsSubstring("spaceBetween"));
}

TEST_CASE("a text patch reports the missing key and the metric that fails", "[diagnostics][json]") {
    vc::clearDiagnostic();
    REQUIRE_FALSE(vc::patchFromJson(R"({"text":{"content":"a","font":"inter-regular-v1"}})").has_value());
    REQUIRE(vc::lastDiagnostic() == vc::Diagnostic{"text.fontSize", "fontSize is required"});

    vc::clearDiagnostic();
    REQUIRE_FALSE(vc::patchFromJson(R"({"textStyle":{"fontSize":900}})").has_value());
    REQUIRE(vc::lastDiagnostic() == vc::Diagnostic{"textStyle", "fontSize must be between 1 and 512"});
}

TEST_CASE("a bad text metric in an imported snapshot points at the node", "[diagnostics][json]") {
    vc::clearDiagnostic();
    REQUIRE_FALSE(vc::snapshotFromJson(snapshot(5, "[" + textNode("0") + "]")).has_value());
    REQUIRE(vc::lastDiagnostic() == vc::Diagnostic{"nodes[0].text", "fontSize must be between 1 and 512"});
}

TEST_CASE("an unknown node type lists the node types", "[diagnostics][json]") {
    vc::clearDiagnostic();
    REQUIRE_FALSE(vc::snapshotFromJson(snapshot(5, "[" + node("blob") + "]")).has_value());
    const auto why = vc::lastDiagnostic();
    REQUIRE(why.has_value());
    REQUIRE(why->path == "nodes[0].type");
    REQUIRE_THAT(why->message, ContainsSubstring("frame, rect, ellipse"));
}

TEST_CASE("a node without an order says how to write one", "[diagnostics][json]") {
    vc::clearDiagnostic();
    REQUIRE_FALSE(vc::snapshotFromJson(snapshot(5, std::string{R"([{"id":")"} + kId + R"(","type":"rect"}])")).has_value());
    const auto why = vc::lastDiagnostic();
    REQUIRE(why.has_value());
    REQUIRE(why->path == "nodes[0].order");
    REQUIRE_THAT(why->message, ContainsSubstring("fractional index"));
}

TEST_CASE("a node with a field its type does not take is refused", "[diagnostics][json]") {
    vc::clearDiagnostic();
    REQUIRE_FALSE(vc::snapshotFromJson(snapshot(12, "[" + node("rect", R"(,"lineReverse":true)") + "]")).has_value());
    REQUIRE(vc::lastDiagnostic() == vc::Diagnostic{"nodes[0].lineReverse", "only line nodes take lineReverse"});
}

TEST_CASE("snapshot version gates name the feature and the version", "[diagnostics][json]") {
    vc::clearDiagnostic();
    REQUIRE_FALSE(vc::snapshotFromJson(snapshot(4, "[" + textNode("16") + "]")).has_value());
    REQUIRE(vc::lastDiagnostic() == vc::Diagnostic{"nodes[0]", "text layers need snapshot version 5 or later"});

    vc::clearDiagnostic();
    REQUIRE_FALSE(vc::snapshotFromJson(snapshot(8, "[" + textNode("16", "inter-bold-v1") + "]")).has_value());
    REQUIRE(vc::lastDiagnostic() == vc::Diagnostic{"nodes[0]", "selecting a font needs snapshot version 9 or later"});

    vc::clearDiagnostic();
    REQUIRE_FALSE(vc::snapshotFromJson(snapshot(13, "[" + node("path", R"(,"path":{"closed":true,"points":[{"x":0,"y":0},{"x":1,"y":0},{"x":1,"y":1}],"filled":true})") + "]")).has_value());
    REQUIRE(vc::lastDiagnostic() == vc::Diagnostic{"nodes[0]", "curved or filled paths need snapshot version 14 or later"});

    vc::clearDiagnostic();
    REQUIRE_FALSE(vc::snapshotFromJson(R"({"version":99,"nodes":[]})").has_value());
    REQUIRE(vc::lastDiagnostic() == vc::Diagnostic{"version", "version must be from 1 to 20"});
}

TEST_CASE("snapshot top level shape is explained", "[diagnostics][json]") {
    vc::clearDiagnostic();
    REQUIRE_FALSE(vc::snapshotFromJson("[]").has_value());
    REQUIRE(vc::lastDiagnostic() == vc::Diagnostic{"", "snapshot must be a JSON object"});

    vc::clearDiagnostic();
    REQUIRE_FALSE(vc::snapshotFromJson(R"({"version":2,"nodes":[]})").has_value());
    REQUIRE(vc::lastDiagnostic() == vc::Diagnostic{"modes", "modes is required from snapshot version 2: an array"});
}

TEST_CASE("a malformed rule in a component definition points at the rule", "[diagnostics][json]") {
    const std::string valid = std::string{R"({"when":{},"target":")"} + kId + R"(","patch":)";
    vc::clearDiagnostic();
    auto definition = [&](const std::string& rule) {
        return snapshot(3, "[" + node("component", R"(,"definition":{"properties":[],"axes":[],"rules":[)" + rule + "]}") + "]");
    };
    REQUIRE_FALSE(vc::snapshotFromJson(definition(valid + R"({"x":"1"}})")).has_value());
    REQUIRE(vc::lastDiagnostic() == vc::Diagnostic{"nodes[0].definition.rules[0].patch.x", "must be a number"});

    vc::clearDiagnostic();
    REQUIRE_FALSE(vc::snapshotFromJson(definition(valid + R"({"bogus":1}})")).has_value());
    const auto why = vc::lastDiagnostic();
    REQUIRE(why.has_value());
    REQUIRE(why->path == "nodes[0].definition.rules[0].patch.bogus");
    REQUIRE_THAT(why->message, ContainsSubstring("not written back"));

    vc::clearDiagnostic();
    REQUIRE_FALSE(vc::snapshotFromJson(definition(R"({"when":{},"target":"nope","patch":{}})")).has_value());
    REQUIRE(vc::lastDiagnostic() == vc::Diagnostic{"nodes[0].definition.rules[0].target", "must be a UUID string"});
}

TEST_CASE("a bad token literal points at the token value", "[diagnostics][json]") {
    const std::string tokens = std::string{R"([{"id":")"} + kId + R"(","type":"color","identifier":"a","order":"a0","value":{"l":0.5,"c":0.1}}])";
    vc::clearDiagnostic();
    REQUIRE_FALSE(vc::snapshotFromJson(snapshot(2, "[]", tokens)).has_value());
    REQUIRE(vc::lastDiagnostic() == vc::Diagnostic{"tokens[0].value.h", "h is required"});

    const std::string unit = std::string{R"([{"id":")"} + kId + R"(","type":"dimension","identifier":"a","order":"a0","value":{"value":4,"unit":"furlong"}}])";
    vc::clearDiagnostic();
    REQUIRE_FALSE(vc::snapshotFromJson(snapshot(2, "[]", unit)).has_value());
    REQUIRE(vc::lastDiagnostic() == vc::Diagnostic{"tokens[0].value.unit", "unit must be px, rem, em, pt, ms, s or empty"});

    vc::clearDiagnostic();
    REQUIRE_FALSE(vc::snapshotFromJson(snapshot(10, "[]", std::string{R"([{"id":")"} + kId + R"(","type":"typography"}])")).has_value());
    REQUIRE(vc::lastDiagnostic() == vc::Diagnostic{"tokens[0].type", "typography tokens need snapshot version 11 or later"});
}

TEST_CASE("an unknown op kind lists the kinds", "[diagnostics][json]") {
    vc::clearDiagnostic();
    REQUIRE_FALSE(vc::opsFromJson(R"([{"kind":"Explode","data":{}}])").has_value());
    const auto why = vc::lastDiagnostic();
    REQUIRE(why.has_value());
    REQUIRE(why->path == "ops[0]");
    REQUIRE_THAT(why->message, ContainsSubstring("unknown op kind Explode"));
    REQUIRE_THAT(why->message, ContainsSubstring("MoveNode"));
}

TEST_CASE("an op with a missing field names it", "[diagnostics][json]") {
    vc::clearDiagnostic();
    REQUIRE_FALSE(vc::opsFromJson(R"([{"kind":"DeleteNode","data":{}}])").has_value());
    const auto why = vc::lastDiagnostic();
    REQUIRE(why.has_value());
    REQUIRE(why->path == "ops[0].id");

    vc::clearDiagnostic();
    const std::string setProps = std::string{R"([{"kind":"SetProps","data":{"id":")"} + kId + R"("}}])";
    REQUIRE_FALSE(vc::opsFromJson(setProps).has_value());
    REQUIRE(vc::lastDiagnostic() == vc::Diagnostic{"ops[0].patch", "patch is required: an object"});

    vc::clearDiagnostic();
    REQUIRE_FALSE(vc::opsFromJson(R"({"kind":"DeleteNode"})").has_value());
    REQUIRE(vc::lastDiagnostic() == vc::Diagnostic{"", "ops must be an array of {kind, data} objects"});
}

TEST_CASE("falling back from a token reference to a literal records nothing", "[diagnostics][json]") {
    vc::clearDiagnostic();
    REQUIRE(vc::patchFromJson(R"({"fill":{"l":0.5,"c":0.1,"h":200,"a":1}})").has_value());
    REQUIRE_FALSE(vc::lastDiagnostic().has_value());

    const std::string reference = std::string{R"({"fill":{"token":")"} + kId + R"("},"cornerRadius":{"token":")" + kId + R"("}})";
    REQUIRE(vc::patchFromJson(reference).has_value());
    REQUIRE_FALSE(vc::lastDiagnostic().has_value());

    REQUIRE(vc::patchFromJson(R"({"cornerRadius":4,"stroke":{"width":1,"color":{"l":0.5,"c":0,"h":0,"a":1}}})").has_value());
    REQUIRE_FALSE(vc::lastDiagnostic().has_value());
}

TEST_CASE("a malformed token reference is reported as such", "[diagnostics][json]") {
    vc::clearDiagnostic();
    REQUIRE_FALSE(vc::patchFromJson(R"({"fill":{"token":"nope"}})").has_value());
    const auto why = vc::lastDiagnostic();
    REQUIRE(why.has_value());
    REQUIRE(why->path == "fill");
    REQUIRE(why->message == "token must be a UUID string");
}
