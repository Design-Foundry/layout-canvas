#include <catch2/catch_test_macros.hpp>
#include <vector>
#include "vc/json.hpp"

using vc::CreateNode;
using vc::Id;
using vc::Node;
using vc::OpBody;

namespace {

Node sampleNode() {
    Node n;
    n.id = *Id::parse("0192a4b1-7c3d-4e5f-8a6b-1c2d3e4f5a6b");
    n.type = vc::NodeType::Frame;
    n.order = "V";
    n.name = "Card";
    n.x = 12.5;
    n.y = -40;
    n.w = 320;
    n.h = 200;
    n.fill = vc::Oklch{0.98, 0.01, 250, 1};
    n.cornerRadius = vc::TokenRef{"radius-md"};
    n.opacity = 0.75;
    n.clipContent = true;
    return n;
}

OpBody roundTrip(const OpBody& op) {
    const auto parsed = vc::opFromJson(vc::opKind(op), vc::opToJson(op));
    REQUIRE(parsed);
    return *parsed;
}

} // namespace

TEST_CASE("every op kind survives a json round trip") {
    const Node node = sampleNode();
    vc::NodePatch patch;
    patch.x = 3;
    patch.fill = vc::TokenRef{"surface-glass-strong"};
    patch.visible = false;

    const OpBody ops[] = {
        CreateNode{node},
        vc::DeleteNode{node.id},
        vc::SetProps{node.id, patch},
        vc::MoveNode{node.id, *Id::parse("0192a4b1-0000-4000-8000-000000000001"), "a"},
        vc::MoveNode{node.id, Id{}, "b"},
    };
    for (const OpBody& op : ops) {
        REQUIRE(roundTrip(op) == op);
    }
}

TEST_CASE("op kinds match the server's names") {
    REQUIRE(vc::opKind(vc::DeleteNode{}) == "DeleteNode");
    REQUIRE(vc::opKind(vc::SetProps{}) == "SetProps");
}

TEST_CASE("malformed op payloads are rejected") {
    REQUIRE_FALSE(vc::opFromJson("CreateNode", "not json"));
    REQUIRE_FALSE(vc::opFromJson("CreateNode", R"({"node":{"id":"x","type":"rect","order":"V"}})"));
    REQUIRE_FALSE(vc::opFromJson("CreateNode",
        R"({"node":{"id":"0192a4b1-7c3d-4e5f-8a6b-1c2d3e4f5a6b","type":"star","order":"V"}})"));
    REQUIRE_FALSE(vc::opFromJson("SetProps",
        R"({"id":"0192a4b1-7c3d-4e5f-8a6b-1c2d3e4f5a6b","patch":{"x":"12"}})"));
    REQUIRE_FALSE(vc::opFromJson("SetProps",
        R"({"id":"0192a4b1-7c3d-4e5f-8a6b-1c2d3e4f5a6b","patch":{"fill":{"l":1}}})"));
    REQUIRE_FALSE(vc::opFromJson("Explode", R"({"id":"0192a4b1-7c3d-4e5f-8a6b-1c2d3e4f5a6b"})"));
}

TEST_CASE("a snapshot rebuilds the same document") {
    vc::Document doc;
    Node frame = sampleNode();
    Node child;
    child.id = *Id::parse("0192a4b1-7c3d-4e5f-8a6b-000000000002");
    child.parent = frame.id;
    child.order = "V";
    child.type = vc::NodeType::Ellipse;
    child.w = child.h = 40;
    doc.apply(CreateNode{frame});
    doc.apply(CreateNode{child});

    const auto ops = vc::snapshotFromJson(vc::snapshotToJson(doc));
    REQUIRE(ops);

    vc::Document copy;
    for (const CreateNode& op : *ops) {
        REQUIRE(copy.apply(op));
    }
    REQUIRE(*copy.find(frame.id) == frame);
    REQUIRE(*copy.find(child.id) == child);
}

TEST_CASE("snapshots with an unknown version are rejected") {
    REQUIRE_FALSE(vc::snapshotFromJson(R"({"version":99,"nodes":[]})"));
    REQUIRE(vc::snapshotFromJson(R"({"version":1,"nodes":[]})"));
}

TEST_CASE("op lists carry ids and kinds and parse back") {
    vc::IdGenerator ids{3};
    const std::vector<vc::Op> ops{
        {ids.next(), CreateNode{sampleNode()}},
        {ids.next(), vc::DeleteNode{sampleNode().id}},
    };

    const auto parsed = vc::opsFromJson(vc::opsToJson(ops));
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->size() == 2);
    REQUIRE((*parsed)[0] == ops[0].body);
    REQUIRE((*parsed)[1] == ops[1].body);
}

TEST_CASE("op lists accept server records and reject bad entries") {
    const auto page = vc::opsFromJson(
        R"([{"seq":4,"id":"0192a4b1-0000-4000-8000-000000000009","kind":"DeleteNode",)"
        R"("data":{"id":"0192a4b1-7c3d-4e5f-8a6b-1c2d3e4f5a6b"},"createdAt":"2026-10-17T00:00:00Z"}])");
    REQUIRE(page.has_value());
    REQUIRE(page->size() == 1);

    REQUIRE_FALSE(vc::opsFromJson(R"([{"kind":"DeleteNode"}])").has_value());
    REQUIRE_FALSE(vc::opsFromJson(R"({"kind":"DeleteNode"})").has_value());
}
