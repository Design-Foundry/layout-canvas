#include <catch2/catch_test_macros.hpp>
#include "vc/diagnostics.hpp"
#include "vc/document.hpp"

using namespace vc;

TEST_CASE("the innermost refusal wins and carries the scoped path") {
    clearDiagnostic();
    {
        const DiagnosticScope nodes{"nodes[4]"};
        const DiagnosticScope field{"text"};
        REQUIRE_FALSE(refuse("letterSpacing must be between 0 and 128"));
    }
    REQUIRE_FALSE(refuse("the snapshot has invalid nodes"));
    REQUIRE(lastDiagnostic() == Diagnostic{"nodes[4].text", "letterSpacing must be between 0 and 128"});
    clearDiagnostic();
    REQUIRE_FALSE(lastDiagnostic());
}

TEST_CASE("index segments attach to the previous one") {
    clearDiagnostic();
    const DiagnosticScope definition{"definition"};
    const DiagnosticScope rules{"rules"};
    const DiagnosticScope index{"[2]"};
    const std::optional<int> value = refused("x");
    REQUIRE_FALSE(value);
    REQUIRE(lastDiagnostic()->path == "definition.rules[2]");
}

TEST_CASE("refused readers return nothing, never a value") {
    const auto number = []() -> std::optional<double> { return refused("n"); };
    const auto flag = []() -> std::optional<bool> { return refused("f"); };
    REQUIRE_FALSE(number().has_value());
    REQUIRE_FALSE(flag().has_value());
}

TEST_CASE("a refused node explains why") {
    Document doc;
    Node text;
    text.id = {9, 1};
    text.type = NodeType::Text;
    text.order = "a0";
    text.text.content = "Hola";
    text.text.letterSpacing = -1;
    clearDiagnostic();
    REQUIRE_FALSE(doc.apply(CreateNode{text}));
    REQUIRE(lastDiagnostic()->message == "text: letterSpacing must be between 0 and 128");
}
