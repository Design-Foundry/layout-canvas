#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <vector>
#include "vc/document.hpp"
#include "vc/fractional_index.hpp"
#include "vc/session.hpp"

using vc::CreateNode;
using vc::DeleteNode;
using vc::Document;
using vc::Id;
using vc::MoveNode;
using vc::Node;
using vc::NodeType;
using vc::Session;
using vc::SetProps;

namespace {

Id idOf(std::uint64_t n) {
    return {0, n};
}

Node node(std::uint64_t id, NodeType type = NodeType::Rect, std::uint64_t parent = 0,
          std::string order = "V") {
    Node n;
    n.id = idOf(id);
    n.type = type;
    n.parent = idOf(parent);
    n.order = std::move(order);
    n.w = 10;
    n.h = 10;
    return n;
}

std::vector<Id> paintOrder(const Document& doc) {
    std::vector<Id> out;
    doc.forEachInPaintOrder([&](const Node& n) { out.push_back(n.id); });
    return out;
}

} // namespace

TEST_CASE("create and delete are each other's inverse") {
    Document doc;
    const auto inverse = doc.apply(CreateNode{node(1)});

    REQUIRE(doc.find(idOf(1)));
    REQUIRE(inverse == vc::OpBody{DeleteNode{idOf(1)}});

    const auto restore = doc.apply(*inverse);
    REQUIRE_FALSE(doc.find(idOf(1)));
    REQUIRE(restore == vc::OpBody{CreateNode{node(1)}});
}

TEST_CASE("invalid ops change nothing") {
    Document doc;
    doc.apply(CreateNode{node(1, NodeType::Frame)});
    doc.apply(CreateNode{node(2, NodeType::Rect, 1)});

    REQUIRE_FALSE(doc.apply(CreateNode{node(1)}));
    REQUIRE_FALSE(doc.apply(CreateNode{node(3, NodeType::Rect, 2)}));
    REQUIRE_FALSE(doc.apply(CreateNode{node(4, NodeType::Rect, 99)}));
    REQUIRE_FALSE(doc.apply(DeleteNode{idOf(1)}));
    REQUIRE_FALSE(doc.apply(DeleteNode{idOf(99)}));

    vc::NodePatch negative;
    negative.w = -1;
    REQUIRE_FALSE(doc.apply(SetProps{idOf(2), negative}));
    REQUIRE(doc.find(idOf(2))->w == 10);
    REQUIRE(doc.size() == 2);
}

TEST_CASE("set props returns the previous values of the touched fields only") {
    Document doc;
    doc.apply(CreateNode{node(1)});
    vc::Token surface;
    surface.id = idOf(100);
    surface.identifier = "surface";
    surface.value = vc::Oklch{0.9, 0.0, 0.0};
    REQUIRE(doc.apply(vc::CreateToken{surface}));

    vc::NodePatch patch;
    patch.x = 50;
    patch.fill = vc::TokenRef{idOf(100)};
    const auto inverse = doc.apply(SetProps{idOf(1), patch});

    const auto& undo = std::get<SetProps>(*inverse).patch;
    REQUIRE(undo.x == 0.0);
    REQUIRE(undo.fill == vc::Value<vc::Oklch>{vc::Oklch{}});
    REQUIRE_FALSE(undo.y);
    REQUIRE(std::get<vc::TokenRef>(doc.find(idOf(1))->fill).token == idOf(100));
}

TEST_CASE("siblings paint in key order, parents before children") {
    Document doc;
    doc.apply(CreateNode{node(1, NodeType::Frame, 0, "V")});
    doc.apply(CreateNode{node(2, NodeType::Rect, 1, "a")});
    doc.apply(CreateNode{node(3, NodeType::Rect, 1, "M")});
    doc.apply(CreateNode{node(4, NodeType::Rect, 0, "A")});

    REQUIRE(paintOrder(doc) == std::vector<Id>{idOf(4), idOf(1), idOf(3), idOf(2)});
}

TEST_CASE("moving reparents and reorders, and refuses cycles") {
    Document doc;
    doc.apply(CreateNode{node(1, NodeType::Frame, 0, "a")});
    doc.apply(CreateNode{node(2, NodeType::Frame, 1, "a")});
    doc.apply(CreateNode{node(3, NodeType::Rect, 0, "b")});

    REQUIRE_FALSE(doc.apply(MoveNode{idOf(1), idOf(2), "a"}));
    REQUIRE_FALSE(doc.apply(MoveNode{idOf(1), idOf(1), "a"}));
    REQUIRE_FALSE(doc.apply(MoveNode{idOf(1), idOf(3), "a"}));

    const auto inverse = doc.apply(MoveNode{idOf(3), idOf(2), "a"});
    REQUIRE(doc.find(idOf(3))->parent == idOf(2));
    REQUIRE(doc.children(idOf(0)) == std::vector<Id>{idOf(1)});

    doc.apply(*inverse);
    REQUIRE(doc.find(idOf(3))->parent == idOf(0));
    REQUIRE(doc.children(idOf(0)) == std::vector<Id>{idOf(1), idOf(3)});
}

TEST_CASE("every local edit lands in the outbox with a fresh id") {
    Session session{vc::IdGenerator{7}};
    session.edit(CreateNode{node(1)});
    session.edit(DeleteNode{idOf(99)});
    vc::NodePatch patch;
    patch.x = 5;
    session.edit(SetProps{idOf(1), patch});

    const auto ops = session.takeOutbox();
    REQUIRE(ops.size() == 2);
    REQUIRE(ops[0].id != ops[1].id);
    REQUIRE(std::holds_alternative<CreateNode>(ops[0].body));
    REQUIRE(session.takeOutbox().empty());
}

TEST_CASE("undo and redo restore the document and emit ops") {
    Session session{vc::IdGenerator{7}};
    session.edit(CreateNode{node(1)});
    vc::NodePatch patch;
    patch.x = 30;
    session.edit(SetProps{idOf(1), patch});
    (void)session.takeOutbox();

    REQUIRE(session.undo());
    REQUIRE(session.document().find(idOf(1))->x == 0);
    REQUIRE(session.undo());
    REQUIRE_FALSE(session.document().find(idOf(1)));
    REQUIRE_FALSE(session.undo());

    REQUIRE(session.redo());
    REQUIRE(session.redo());
    REQUIRE(session.document().find(idOf(1))->x == 30);
    REQUIRE(session.takeOutbox().size() == 4);
}

TEST_CASE("a new edit clears the redo stack") {
    Session session;
    session.edit(CreateNode{node(1)});
    session.undo();
    session.edit(CreateNode{node(2)});

    REQUIRE_FALSE(session.canRedo());
}

TEST_CASE("grouped edits undo as one step") {
    Session session;
    session.begin();
    session.edit(CreateNode{node(1, NodeType::Frame)});
    session.edit(CreateNode{node(2, NodeType::Rect, 1)});
    session.commit();

    session.undo();
    REQUIRE(session.document().size() == 0);
    session.redo();
    REQUIRE(session.document().size() == 2);
}

TEST_CASE("deleting a tree removes descendants first and undoes as one step") {
    Session session;
    session.edit(CreateNode{node(1, NodeType::Frame)});
    session.edit(CreateNode{node(2, NodeType::Frame, 1)});
    session.edit(CreateNode{node(3, NodeType::Rect, 2)});
    (void)session.takeOutbox();

    REQUIRE(session.deleteTree(idOf(1)));
    REQUIRE(session.document().size() == 0);

    const auto ops = session.takeOutbox();
    REQUIRE(ops.size() == 3);
    REQUIRE(std::get<DeleteNode>(ops[0].body).id == idOf(3));

    session.undo();
    REQUIRE(session.document().size() == 3);
    REQUIRE(session.document().find(idOf(3))->parent == idOf(2));
}

TEST_CASE("remote ops are applied but never undone or resent") {
    Session session;
    REQUIRE(session.applyRemote(CreateNode{node(1)}));

    REQUIRE_FALSE(session.canUndo());
    REQUIRE(session.takeOutbox().empty());
}

namespace {

SetProps moveTo(std::uint64_t id, double x) {
    vc::NodePatch patch;
    patch.x = x;
    return {idOf(id), patch};
}

/// Two clients that both start from the same frame with a rect inside.
struct Pair {
    Session a{vc::IdGenerator{1}};
    Session b{vc::IdGenerator{2}};

    Pair() {
        for (Session* s : {&a, &b}) {
            s->applyRemote(CreateNode{node(1, NodeType::Frame)});
            s->applyRemote(CreateNode{node(2, NodeType::Rect, 1)});
        }
    }
};

} // namespace

TEST_CASE("own edits coming back from the log settle the session without touching the document") {
    Pair p;
    p.a.edit(moveTo(2, 5));
    REQUIRE_FALSE(p.a.isSettled());
    const auto log = p.a.takeOutbox();
    const auto revision = p.a.revision();

    REQUIRE(p.a.receive(log) == 1);
    REQUIRE(p.a.isSettled());
    REQUIRE(p.a.revision() == revision);
    REQUIRE(p.a.document().find(idOf(2))->x == 5);
}

TEST_CASE("clients converge on the log order when they edit the same property") {
    Pair p;
    p.a.edit(moveTo(2, 5));
    p.b.edit(moveTo(2, 9));
    // b reaches the server first, so a's edit lands on top of it.
    std::vector<vc::Op> log = p.b.takeOutbox();
    for (vc::Op& op : p.a.takeOutbox()) log.push_back(op);

    p.a.receive(log);
    p.b.receive(log);
    REQUIRE(p.a.document().find(idOf(2))->x == 5);
    REQUIRE(p.b.document().find(idOf(2))->x == 5);
    REQUIRE(p.a.isSettled());
    REQUIRE(p.b.isSettled());
}

TEST_CASE("local edits stay on top of remote ops until the log confirms them") {
    Pair p;
    p.a.edit(moveTo(2, 5));
    const auto mine = p.a.takeOutbox();
    p.b.edit(moveTo(2, 9));

    p.a.receive(p.b.takeOutbox());
    REQUIRE(p.a.document().find(idOf(2))->x == 5);
    REQUIRE_FALSE(p.a.isSettled());

    p.a.receive(mine);
    REQUIRE(p.a.isSettled());
    REQUIRE(p.a.document().find(idOf(2))->x == 5);
}

TEST_CASE("a local edit that no longer applies after remote ops is dropped and never sent") {
    Pair p;
    p.a.edit(CreateNode{node(3, NodeType::Rect, 1, "a")});
    p.b.deleteTree(idOf(1));

    p.a.receive(p.b.takeOutbox());
    REQUIRE(p.a.document().size() == 0);
    REQUIRE(p.a.isSettled());
    REQUIRE(p.a.takeOutbox().empty());
}

TEST_CASE("a preview survives remote ops arriving beneath it") {
    Pair p;
    REQUIRE(p.a.preview(moveTo(2, 40)));
    REQUIRE_FALSE(p.a.isSettled());
    vc::NodePatch rename;
    rename.name = "Card";
    p.b.edit(SetProps{idOf(1), rename});

    p.a.receive(p.b.takeOutbox());
    REQUIRE(p.a.hasPreview());
    REQUIRE(p.a.document().find(idOf(2))->x == 40);
    REQUIRE(p.a.document().find(idOf(1))->name == "Card");

    p.a.cancelPreview();
    REQUIRE(p.a.document().find(idOf(2))->x == 0);
    REQUIRE(p.a.isSettled());
}

TEST_CASE("undone edits travel and confirm like any other edit") {
    Pair p;
    p.a.edit(moveTo(2, 5));
    p.a.undo();
    const auto log = p.a.takeOutbox();
    REQUIRE(log.size() == 2);

    p.b.receive(log);
    REQUIRE(p.b.document().find(idOf(2))->x == 0);
    p.a.receive(log);
    REQUIRE(p.a.isSettled());
}
