#include <catch2/catch_test_macros.hpp>
#include <string>
#include "vc/diagnostics.hpp"
#include "vc/document.hpp"
#include "vc/json.hpp"
#include "vc/session.hpp"

using namespace vc;

namespace {

constexpr const char* kFrameId = "0192a4b1-7c3d-4e5f-8a6b-000000000002";

Id idOf(std::uint64_t n) {
    return {4, n};
}

Node frame(std::uint64_t id, const char* order = "a0") {
    Node n;
    n.id = idOf(id);
    n.type = NodeType::Frame;
    n.order = order;
    n.name = "Card";
    n.w = 100;
    n.h = 50;
    return n;
}

std::string snapshot(int version, const std::string& extra) {
    return R"({"version":)" + std::to_string(version) + R"(,"modes":[],"tokens":[],"nodes":[{"id":")" + kFrameId +
           R"(","type":"frame","order":"a0")" + extra + "}]}";
}

NodePatch withNotes(const std::string& text) {
    NodePatch patch;
    patch.notes = text;
    return patch;
}

} // namespace

TEST_CASE("notes are set, cleared, undone and redone", "[notes]") {
    Session session{IdGenerator{9}};
    REQUIRE(session.edit(CreateNode{frame(1)}));
    REQUIRE(session.document().find(idOf(1))->notes.empty());

    REQUIRE(session.edit(SetProps{idOf(1), withNotes("Check the copy\nwith legal")}));
    REQUIRE(session.document().find(idOf(1))->notes == "Check the copy\nwith legal");
    REQUIRE(session.edit(SetProps{idOf(1), withNotes("")}));
    REQUIRE(session.document().find(idOf(1))->notes.empty());

    REQUIRE(session.undo());
    REQUIRE(session.document().find(idOf(1))->notes == "Check the copy\nwith legal");
    REQUIRE(session.undo());
    REQUIRE(session.document().find(idOf(1))->notes.empty());
    REQUIRE(session.redo());
    REQUIRE(session.document().find(idOf(1))->notes == "Check the copy\nwith legal");
}

TEST_CASE("notes can be given when a node is created and survive deleting and undoing", "[notes]") {
    Session session{IdGenerator{9}};
    Node card = frame(1);
    card.notes = "From the brief";
    REQUIRE(session.edit(CreateNode{card}));
    REQUIRE(session.edit(DeleteNode{idOf(1)}));
    REQUIRE(session.undo());
    REQUIRE(session.document().find(idOf(1))->notes == "From the brief");
}

TEST_CASE("notes are bounded plain text", "[notes]") {
    Document doc;
    REQUIRE(doc.apply(CreateNode{frame(1)}));
    REQUIRE(doc.apply(SetProps{idOf(1), withNotes(std::string(2048, 'a'))}));
    clearDiagnostic();
    REQUIRE_FALSE(doc.apply(SetProps{idOf(1), withNotes(std::string(2049, 'a'))}));
    REQUIRE(lastDiagnostic()->message.find("2048 characters") != std::string::npos);
    REQUIRE_FALSE(doc.apply(SetProps{idOf(1), withNotes("tab\there")}));
    REQUIRE_FALSE(doc.apply(SetProps{idOf(1), withNotes("bad \xff bytes")}));
    REQUIRE(doc.find(idOf(1))->notes == std::string(2048, 'a'));
    Node card = frame(2, "a1");
    card.notes = std::string(4097, 'a');
    REQUIRE_FALSE(doc.apply(CreateNode{card}));
}

TEST_CASE("notes go through patch JSON and node inspection", "[notes][json]") {
    const auto patch = patchFromJson(R"({"notes":"one\ntwo"})");
    REQUIRE(patch);
    REQUIRE(patch->notes == "one\ntwo");
    REQUIRE_FALSE(patchFromJson(R"({"notes":4})").has_value());
    REQUIRE_FALSE(patchFromJson(std::string{R"({"notes":")"} + std::string(4097, 'a') + R"("})").has_value());

    Document doc;
    Node card = frame(1);
    card.notes = "keep";
    REQUIRE(doc.apply(CreateNode{card}));
    const std::string inspected = inspectNodeJson(doc, *doc.find(idOf(1)), kDefaultModeId);
    REQUIRE(inspected.find("\"notes\":\"keep\"") != std::string::npos);
    REQUIRE(inspected.find("textSize") == std::string::npos);
}

TEST_CASE("a snapshot with notes is version 26 and round trips", "[notes][json]") {
    Document doc;
    Node card = frame(1);
    card.notes = "Line one\nLine two \xc3\xa9";
    REQUIRE(doc.apply(CreateNode{card}));
    const std::string json = snapshotToJson(doc);
    REQUIRE(json.find("\"version\":26") != std::string::npos);

    Document copy;
    const auto ops = snapshotFromJson(json);
    REQUIRE(ops.has_value());
    for (const OpBody& op : *ops) REQUIRE(copy.apply(op));
    REQUIRE(copy.find(idOf(1))->notes == card.notes);
    REQUIRE(snapshotToJson(copy) == json);
}

TEST_CASE("a document without notes keeps its older version", "[notes][json]") {
    Document doc;
    REQUIRE(doc.apply(CreateNode{frame(1)}));
    REQUIRE(snapshotToJson(doc).find("\"version\":2}") != std::string::npos);

    REQUIRE(doc.apply(SetProps{idOf(1), withNotes("x")}));
    REQUIRE(snapshotToJson(doc).find("\"version\":26") != std::string::npos);
    REQUIRE(doc.apply(SetProps{idOf(1), withNotes("")}));
    REQUIRE(snapshotToJson(doc).find("\"version\":2}") != std::string::npos);
}

TEST_CASE("snapshot 25 loads without notes and older snapshots refuse them", "[notes][json]") {
    const auto loaded = snapshotFromJson(snapshot(25, ""));
    REQUIRE(loaded.has_value());
    Document doc;
    for (const OpBody& op : *loaded) REQUIRE(doc.apply(op));
    REQUIRE(doc.find(*Id::parse(kFrameId))->notes.empty());

    clearDiagnostic();
    REQUIRE_FALSE(snapshotFromJson(snapshot(25, R"(,"notes":"a")")).has_value());
    REQUIRE(lastDiagnostic() == Diagnostic{"nodes[0]", "node notes need snapshot version 26 or later"});
    REQUIRE(snapshotFromJson(snapshot(26, R"(,"notes":"a")")).has_value());
    clearDiagnostic();
    REQUIRE_FALSE(snapshotFromJson(snapshot(26, R"(,"notes":")" + std::string(4097, 'a') + R"(")")).has_value());
    REQUIRE(lastDiagnostic()->path == "nodes[0].notes");
}

TEST_CASE("cloning keeps notes on the copy and the source", "[notes]") {
    Session session{IdGenerator{9}};
    Node card = frame(1);
    card.notes = "Primary card";
    REQUIRE(session.edit(CreateNode{card}));
    Node child = frame(2);
    child.parent = idOf(1);
    child.notes = "Inner note";
    REQUIRE(session.edit(CreateNode{child}));

    REQUIRE(session.edit(CloneNode{idOf(1), idOf(10), Id{}, "a1", {}}));
    const Id inner = derivedId(idOf(10), idOf(2));
    REQUIRE(session.document().find(idOf(10))->notes == "Primary card");
    REQUIRE(session.document().find(inner)->notes == "Inner note");
    REQUIRE(session.document().find(idOf(1))->notes == "Primary card");
    REQUIRE(session.undo());
    REQUIRE(session.document().find(idOf(10)) == nullptr);
}

TEST_CASE("overrides and rules cannot set notes", "[notes]") {
    Document doc;
    Node main = frame(1);
    main.type = NodeType::Component;
    REQUIRE(doc.apply(CreateNode{main}));
    Node layer = frame(2);
    layer.parent = idOf(1);
    REQUIRE(doc.apply(CreateNode{layer}));
    Node instance = frame(3, "a1");
    instance.type = NodeType::Instance;
    instance.component = idOf(1);
    REQUIRE(doc.apply(CreateNode{instance}));
    REQUIRE_FALSE(doc.apply(SetOverride{idOf(3), idOf(2), withNotes("x")}));
    REQUIRE(doc.apply(SetProps{idOf(3), withNotes("instance note")}));
    REQUIRE(doc.view(idOf(3))->notes == "instance note");
}
