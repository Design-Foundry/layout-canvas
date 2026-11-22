#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <memory>
#include <string>
#include "vc/diagnostics.hpp"
#include "vc/document.hpp"
#include "vc/editor.hpp"
#include "vc/json.hpp"
#include "vc/native.h"
#include "vc/session.hpp"

using namespace vc;

namespace {

Id idOf(std::uint64_t n) {
    return {3, n};
}

constexpr std::uint64_t kFrame = 1, kText = 2, kRect = 3, kInner = 4, kInnerText = 5, kInstance = 6, kOther = 30, kMain = 10,
                        kMainChild = 11, kToken = 90;

Node make(std::uint64_t id, NodeType type, std::uint64_t parent, const char* order, const char* name) {
    Node n;
    n.id = idOf(id);
    n.type = type;
    n.parent = parent ? idOf(parent) : Id{};
    n.order = order;
    n.name = name;
    n.w = 40;
    n.h = 20;
    return n;
}

Interaction navigate(std::uint64_t target) {
    Interaction i;
    i.action = PrototypeAction::Navigate;
    i.target = idOf(target);
    return i;
}

struct Scene {
    Document doc;

    Scene() {
        Token token;
        token.id = idOf(kToken);
        token.identifier = "brand";
        token.value = Oklch{0.6, 0.1, 200.0, 1.0};
        REQUIRE(doc.apply(CreateToken{token}));

        Node main = make(kMain, NodeType::Component, 0, "a0", "Chip");
        REQUIRE(doc.apply(CreateNode{main}));
        REQUIRE(doc.apply(CreateNode{make(kMainChild, NodeType::Rect, kMain, "a0", "Dot")}));

        Node other = make(kOther, NodeType::Frame, 0, "a1", "Other");
        REQUIRE(doc.apply(CreateNode{other}));

        Node frame = make(kFrame, NodeType::Frame, 0, "a2", "Card");
        frame.fill = TokenRef{idOf(kToken)};
        frame.layout.type = LayoutType::Column;
        frame.layout.gap = 8.0;
        frame.prototypeStart = true;
        frame.prototypeFlowName = "Flow";
        REQUIRE(doc.apply(CreateNode{frame}));

        Node text = make(kText, NodeType::Text, kFrame, "a0", "Title");
        text.text.content = "Hello";
        text.interactions.push_back({PrototypeTrigger::Click, navigate(kOther)});
        REQUIRE(doc.apply(CreateNode{text}));

        Node rect = make(kRect, NodeType::Rect, kFrame, "a1", "Button");
        rect.fill = TokenRef{idOf(kToken)};
        rect.interaction = navigate(kFrame);
        REQUIRE(doc.apply(CreateNode{rect}));

        REQUIRE(doc.apply(CreateNode{make(kInner, NodeType::Frame, kFrame, "a2", "Row")}));
        Node innerText = make(kInnerText, NodeType::Text, kInner, "a0", "Label");
        innerText.text.content = "Label";
        REQUIRE(doc.apply(CreateNode{innerText}));

        Node instance = make(kInstance, NodeType::Instance, kFrame, "a3", "Chip");
        instance.component = idOf(kMain);
        REQUIRE(doc.apply(CreateNode{instance}));
        NodePatch override;
        override.opacity = 0.5;
        REQUIRE(doc.apply(SetOverride{idOf(kInstance), idOf(kMainChild), override}));
    }

    CloneNode clone(std::uint64_t id = 100, std::uint64_t parent = 0) const {
        return CloneNode{idOf(kFrame), idOf(id), parent ? idOf(parent) : Id{}, "a5", {}};
    }

    Id copyOf(std::uint64_t source, std::uint64_t root = 100) const {
        return source == kFrame ? idOf(root) : derivedId(idOf(root), idOf(source));
    }
};

void load(Session& session, const Document& source) {
    const auto ops = snapshotFromJson(snapshotToJson(source));
    REQUIRE(ops.has_value());
    for (const OpBody& op : *ops) REQUIRE(session.applyRemote(op));
}

std::string failure(Document& doc, const OpBody& op) {
    const std::string before = snapshotToJson(doc);
    clearDiagnostic();
    REQUIRE_FALSE(doc.apply(op));
    REQUIRE(snapshotToJson(doc) == before);
    const auto diagnostic = lastDiagnostic();
    REQUIRE(diagnostic);
    return diagnostic->path + ": " + diagnostic->message;
}

} // namespace

TEST_CASE("derived ids depend only on the clone root and the source id") {
    const Id a = derivedId(idOf(1), idOf(2));
    REQUIRE(a == derivedId(idOf(1), idOf(2)));
    REQUIRE(a != derivedId(idOf(2), idOf(2)));
    REQUIRE(a != derivedId(idOf(1), idOf(3)));
    REQUIRE(Id::parse(a.toString()) == a);
    REQUIRE(a.toString()[14] == '4');
}

TEST_CASE("cloning a frame copies its subtree under derived ids") {
    Scene scene;
    const auto before = scene.doc.size();
    REQUIRE(scene.doc.apply(scene.clone()));
    REQUIRE(scene.doc.size() == before + 6);

    const Node& root = *scene.doc.find(idOf(100));
    REQUIRE(root.name == "Card");
    REQUIRE(root.order == "a5");
    REQUIRE(root.parent.isNil());
    REQUIRE(root.fill == Value<Oklch>{TokenRef{idOf(kToken)}});
    REQUIRE(root.layout.type == LayoutType::Column);
    REQUIRE(root.layout.gap == Value<double>{8.0});

    const auto kids = scene.doc.children(root.id);
    REQUIRE(kids == std::vector<Id>{scene.copyOf(kText), scene.copyOf(kRect), scene.copyOf(kInner), scene.copyOf(kInstance)});
    REQUIRE(scene.doc.find(scene.copyOf(kText))->text.content == "Hello");
    REQUIRE(scene.doc.find(scene.copyOf(kRect))->fill == Value<Oklch>{TokenRef{idOf(kToken)}});
    REQUIRE(scene.doc.find(scene.copyOf(kInnerText))->parent == scene.copyOf(kInner));
    for (const std::uint64_t source : {kText, kRect, kInner, kInnerText, kInstance}) {
        REQUIRE(scene.doc.find(scene.copyOf(source))->order == scene.doc.find(idOf(source))->order);
    }

    const Node& instance = *scene.doc.find(scene.copyOf(kInstance));
    REQUIRE(instance.type == NodeType::Instance);
    REQUIRE(instance.component == idOf(kMain));
    REQUIRE(instance.overrides == scene.doc.find(idOf(kInstance))->overrides);
}

TEST_CASE("references inside the subtree follow the copies and references outside stay") {
    Scene scene;
    REQUIRE(scene.doc.apply(scene.clone()));

    REQUIRE(scene.doc.find(scene.copyOf(kRect))->interaction.target == idOf(100));
    REQUIRE(scene.doc.find(idOf(kRect))->interaction.target == idOf(kFrame));
    REQUIRE(scene.doc.find(scene.copyOf(kText))->interactions.at(0).interaction.target == idOf(kOther));
}

TEST_CASE("parts and prototype starts are not copied") {
    Scene scene;
    REQUIRE(scene.doc.find(idOf(kFrame))->prototypeStart);
    REQUIRE(scene.doc.apply(scene.clone()));
    REQUIRE_FALSE(scene.doc.find(idOf(100))->prototypeStart);
    REQUIRE(scene.doc.find(idOf(100))->prototypeFlowName.empty());
}

TEST_CASE("patches change the copies and leave the source alone") {
    Scene scene;
    CloneNode op = scene.clone();
    NodePatch rename;
    rename.name = "Card 2";
    NodePatch retext;
    TextRun run = scene.doc.find(idOf(kText))->text;
    run.content = "Goodbye";
    retext.text = run;
    NodePatch label;
    label.text = run;
    label.text->content = "Other";
    op.patches = {{idOf(kFrame), rename}, {idOf(kText), retext}, {idOf(kInnerText), label}};
    std::sort(op.patches.begin(), op.patches.end(), [](const Override& a, const Override& b) { return a.node < b.node; });
    REQUIRE(scene.doc.apply(op));

    REQUIRE(scene.doc.find(idOf(100))->name == "Card 2");
    REQUIRE(scene.doc.find(scene.copyOf(kText))->text.content == "Goodbye");
    REQUIRE(scene.doc.find(scene.copyOf(kInnerText))->text.content == "Other");
    REQUIRE(scene.doc.find(idOf(kFrame))->name == "Card");
    REQUIRE(scene.doc.find(idOf(kText))->text.content == "Hello");
}

TEST_CASE("a patch can set a text property on a cloned instance") {
    Scene scene;
    CloneNode op = scene.clone();
    NodePatch patch;
    patch.opacity = 0.25;
    op.patches = {{idOf(kInstance), patch}};
    REQUIRE(scene.doc.apply(op));
    REQUIRE(scene.doc.find(scene.copyOf(kInstance))->opacity == Value<double>{0.25});
    REQUIRE(scene.doc.find(idOf(kInstance))->opacity == Value<double>{1.0});
}

TEST_CASE("a clone refuses what it cannot do, names where, and changes nothing") {
    Scene scene;

    CloneNode op = scene.clone();
    op.source = idOf(999);
    REQUIRE(failure(scene.doc, op) == "source: no node has this id");

    op = scene.clone(kRect);
    REQUIRE(failure(scene.doc, op) == "id: a node with this id already exists");

    op = scene.clone(100, 999);
    REQUIRE(failure(scene.doc, op) == "parent: the parent does not exist or cannot have children");

    op = scene.clone(100, kRect);
    REQUIRE(failure(scene.doc, op) == "parent: the parent does not exist or cannot have children");

    op = scene.clone();
    op.id = Id{};
    REQUIRE(failure(scene.doc, op) == "id: a node needs an id");

    op = scene.clone();
    NodePatch patch;
    patch.name = "x";
    op.patches = {{idOf(kOther), patch}};
    REQUIRE(failure(scene.doc, op) == "patches." + idOf(kOther).toString() + ": not the source or one of its descendants");

    op = scene.clone();
    NodePatch wrong;
    wrong.lineWidth = 4.0;
    NodePatch fine;
    fine.name = "fine";
    op.patches = {{idOf(kFrame), fine}, {idOf(kText), wrong}};
    std::sort(op.patches.begin(), op.patches.end(), [](const Override& a, const Override& b) { return a.node < b.node; });
    REQUIRE(failure(scene.doc, op) == "patches." + idOf(kText).toString() + ": only line and path nodes take lineWidth");

    op = scene.clone();
    NodePatch negative;
    negative.w = -1.0;
    op.patches = {{idOf(kInner), negative}};
    REQUIRE(failure(scene.doc, op) == "patches." + idOf(kInner).toString() + ": w and h cannot be negative");
}

TEST_CASE("a clone onto an occupied derived id is refused") {
    Scene scene;
    Node squatter = make(1, NodeType::Rect, 0, "a9", "Squatter");
    squatter.id = derivedId(idOf(100), idOf(kText));
    REQUIRE(scene.doc.apply(CreateNode{squatter}));
    REQUIRE(failure(scene.doc, scene.clone()) == "id: the id of the copy of " + idOf(kText).toString() + " already exists");
}

TEST_CASE("main components cannot be cloned, alone or inside a subtree") {
    Scene scene;
    CloneNode op{idOf(kMain), idOf(100), Id{}, "a5", {}};
    REQUIRE(failure(scene.doc, op).starts_with("source: a main component cannot be cloned"));

    Document doc;
    REQUIRE(doc.apply(CreateNode{make(1, NodeType::Frame, 0, "a0", "Holder")}));
    REQUIRE(doc.apply(CreateNode{make(2, NodeType::Component, 1, "a0", "Inside")}));
    REQUIRE(failure(doc, CloneNode{idOf(1), idOf(100), Id{}, "a5", {}}).starts_with("source: a main component cannot be cloned"));
}

TEST_CASE("a clone into a nested parent keeps a root frame destination valid or refuses") {
    Scene scene;
    CloneNode op = scene.clone(100, kInner);
    REQUIRE(failure(scene.doc, op).find("a prototype destination must be an existing root frame") != std::string::npos);
}

TEST_CASE("a clone can go inside the frame it came from") {
    Scene scene;
    Document& doc = scene.doc;
    CloneNode op{idOf(kInner), idOf(100), idOf(kFrame), "b0", {}};
    REQUIRE(doc.apply(op));
    REQUIRE(doc.find(idOf(100))->parent == idOf(kFrame));
    REQUIRE(doc.find(derivedId(idOf(100), idOf(kInnerText)))->parent == idOf(100));
}

TEST_CASE("the inverse of a clone removes the subtree and its inverse brings it back") {
    Scene scene;
    const std::string before = snapshotToJson(scene.doc);
    const auto inverse = scene.doc.apply(scene.clone());
    REQUIRE(inverse);
    REQUIRE(std::holds_alternative<Batch>(*inverse));
    const std::string after = snapshotToJson(scene.doc);

    const auto redo = scene.doc.apply(*inverse);
    REQUIRE(redo);
    REQUIRE(snapshotToJson(scene.doc) == before);
    REQUIRE(scene.doc.apply(*redo));
    REQUIRE(snapshotToJson(scene.doc) == after);
}

TEST_CASE("sessions undo and redo a clone as one step") {
    Session session{IdGenerator{5}};
    Scene scene;
    load(session, scene.doc);
    const std::string before = snapshotToJson(session.document());

    REQUIRE(session.edit(scene.clone()));
    const std::string after = snapshotToJson(session.document());
    REQUIRE(after != before);

    REQUIRE(session.undo());
    REQUIRE(snapshotToJson(session.document()) == before);
    REQUIRE(session.redo());
    REQUIRE(snapshotToJson(session.document()) == after);
    REQUIRE(session.undo());
    REQUIRE(snapshotToJson(session.document()) == before);
}

TEST_CASE("replaying a clone gives every replica the same document") {
    Scene a;
    Scene b;
    CloneNode op = a.clone();
    NodePatch rename;
    rename.name = "Copy";
    op.patches = {{idOf(kFrame), rename}};
    REQUIRE(a.doc.apply(op));
    const auto sent = opFromJson(opKind(op), opToJson(op));
    REQUIRE(sent);
    REQUIRE(b.doc.apply(*sent));
    REQUIRE(snapshotToJson(a.doc) == snapshotToJson(b.doc));
}

TEST_CASE("a batch is all or nothing") {
    Scene scene;
    const std::string before = snapshotToJson(scene.doc);
    Batch batch;
    batch.ops.emplace_back(CreateNode{make(200, NodeType::Rect, 0, "a7", "A")});
    batch.ops.emplace_back(CreateNode{make(201, NodeType::Rect, 999, "a7", "B")});
    REQUIRE(failure(scene.doc, batch).find("the parent does not exist") != std::string::npos);
    REQUIRE(snapshotToJson(scene.doc) == before);
}

TEST_CASE("clone ops and batches round-trip through JSON") {
    NodePatch patch;
    patch.name = "Two";
    patch.x = 12.0;
    CloneNode clone{idOf(1), idOf(2), idOf(3), "a1", {{idOf(1), patch}, {idOf(4), patch}}};
    const auto parsed = opFromJson(opKind(clone), opToJson(clone));
    REQUIRE(parsed);
    REQUIRE(*parsed == OpBody{clone});

    clone.parent = Id{};
    clone.patches.clear();
    REQUIRE(*opFromJson("CloneNode", opToJson(clone)) == OpBody{clone});
    REQUIRE(opToJson(clone).find("parent") == std::string::npos);

    Batch batch;
    batch.ops.emplace_back(CreateNode{make(1, NodeType::Rect, 0, "a0", "A")});
    batch.ops.emplace_back(DeleteNode{idOf(1)});
    REQUIRE(opKind(batch) == "Batch");
    REQUIRE(*opFromJson("Batch", opToJson(batch)) == OpBody{batch});
}

TEST_CASE("malformed clone ops name the exact path") {
    const std::string a = idOf(1).toString(), b = idOf(2).toString();
    const auto path = [&](const std::string& data) {
        clearDiagnostic();
        REQUIRE_FALSE(opFromJson("CloneNode", data));
        const auto diagnostic = lastDiagnostic();
        REQUIRE(diagnostic);
        return diagnostic->path + ": " + diagnostic->message;
    };
    REQUIRE(path(R"({"id":")" + b + R"(","order":"a0"})") == "source: source is required: a UUID string");
    REQUIRE(path(R"({"id":")" + b + R"(","source":")" + a + R"("})") == "order: order is required: a fractional index string such as a0");
    REQUIRE(path(R"({"id":")" + b + R"(","source":")" + a + R"(","order":"a0","parent":"x"})") == "parent: must be a UUID string");
    REQUIRE(path(R"({"id":")" + b + R"(","source":")" + a + R"(","order":"a0","patches":[]})").starts_with("patches: patches must be an object"));
    REQUIRE(path(R"({"id":")" + b + R"(","source":")" + a + R"(","order":"a0","patches":{"nope":{}}})").starts_with("patches.nope: the key must be the UUID"));
    REQUIRE(path(R"({"id":")" + b + R"(","source":")" + a + R"(","order":"a0","patches":{")" + a + R"(":{"w":"wide"}}})") == "patches." + a + ".w: must be a number");
    REQUIRE(path(R"({"id":")" + b + R"(","source":")" + a + R"(","order":"a0","patches":{")" + a + R"(":5}})") == "patches." + a + ": must be a patch object");

    clearDiagnostic();
    REQUIRE_FALSE(opFromJson("Batch", R"({"ops":[{"kind":"SetProps","data":{"id":")" + a + R"(","patch":{}}}]})"));
    REQUIRE(lastDiagnostic()->path == "ops[0]");
    REQUIRE(lastDiagnostic()->message == "a batch holds only CreateNode and DeleteNode ops");
}

namespace {

struct Closer {
    void operator()(vc_document* document) const { vc_document_close(document); }
};

std::string take(char* text) {
    REQUIRE(text != nullptr);
    std::string out = text;
    vc_free(text);
    return out;
}

} // namespace

TEST_CASE("the native library applies a clone and reports a refused patch with its path") {
    std::unique_ptr<vc_document, Closer> doc{vc_document_open(nullptr, nullptr)};
    Scene scene;
    std::string ops = "[";
    const auto loaded = snapshotFromJson(snapshotToJson(scene.doc));
    REQUIRE(loaded.has_value());
    for (const OpBody& op : *loaded) {
        if (ops.size() > 1) ops += ',';
        ops += R"({"kind":")" + std::string{opKind(op)} + R"(","data":)" + opToJson(op) + "}";
    }
    const std::string good = R"({"kind":"CloneNode","data":{"source":")" + idOf(kFrame).toString() + R"(","id":")" + idOf(100).toString() +
                             R"(","order":"a5","patches":{")" + idOf(kFrame).toString() + R"(":{"name":"Copy"}}}})";
    const std::string bad = R"({"kind":"CloneNode","data":{"source":")" + idOf(kFrame).toString() + R"(","id":")" + idOf(101).toString() +
                            R"(","order":"a6","patches":{")" + idOf(kText).toString() + R"(":{"lineWidth":3}}}})";
    const std::string result = take(vc_document_apply(doc.get(), (ops + "," + good + "," + bad + "]").c_str()));
    REQUIRE(result.find(R"(,true,{"path":"ops[)") != std::string::npos);
    REQUIRE(result.find(R"("path":"ops[)" + std::to_string(loaded->size() + 1) +
                        "].patches." + idOf(kText).toString() + R"(","message":"only line and path nodes take lineWidth")") != std::string::npos);
    const std::string snapshot = take(vc_document_snapshot(doc.get()));
    REQUIRE(snapshot.find(idOf(100).toString()) != std::string::npos);
    REQUIRE(snapshot.find(idOf(101).toString()) == std::string::npos);
}

TEST_CASE("duplicating the selection clones it just above and offset, and undoes as one step") {
    Session session{IdGenerator{8}};
    Camera camera;
    Editor editor{session, camera};
    Scene scene;
    load(session, scene.doc);
    const std::string before = snapshotToJson(session.document());

    editor.select(idOf(kFrame));
    REQUIRE(editor.duplicateSelection());
    REQUIRE(editor.selection().size() == 1);
    const Id copy = editor.selection().at(0);
    REQUIRE(copy != idOf(kFrame));
    const Node& node = *session.document().find(copy);
    REQUIRE(node.name == "Card");
    REQUIRE(node.x == session.document().find(idOf(kFrame))->x + 16.0);
    REQUIRE(node.order > session.document().find(idOf(kFrame))->order);
    REQUIRE(session.document().children(copy).size() == 4);

    REQUIRE(editor.undo());
    REQUIRE(snapshotToJson(session.document()) == before);
    REQUIRE(editor.redo());
    REQUIRE(session.document().find(copy));

    editor.select(idOf(kMain));
    REQUIRE_FALSE(editor.duplicateSelection());
    editor.clearSelection();
    REQUIRE_FALSE(editor.duplicateSelection());
}

