#include <catch2/catch_test_macros.hpp>
#include "vc/editor.hpp"
#include "vc/geometry.hpp"
#include "vc/json.hpp"

using vc::CreateNode;
using vc::Editor;
using vc::Id;
using vc::Node;
using vc::NodeType;
using vc::Oklch;
using vc::PointerButton;
using vc::Session;
using vc::Tool;

namespace {

struct Fixture {
    Session session{vc::IdGenerator{21}};
    vc::Camera camera;
    Editor editor{session, camera};
    Id component;
    Id badge;

    Fixture() {
        editor.setTool(Tool::Frame);
        drag({0, 0}, {200, 100});
        editor.setTool(Tool::Rect);
        drag({10, 10}, {60, 40});
        badge = editor.selection().at(0);
        editor.select(session.document().find(badge)->parent);
        component = *editor.createComponent();
    }

    void drag(vc::Vec2 from, vc::Vec2 to) {
        editor.pointerDown(from, PointerButton::Primary);
        editor.pointerMove(to);
        editor.pointerUp();
    }

    const Node& view(const Id& key) const { return *session.document().view(key); }
};

} // namespace

TEST_CASE("a frame becomes a component and an instance copies its tree") {
    Fixture f;
    REQUIRE(f.session.document().find(f.component)->type == NodeType::Component);

    const Id instance = *f.editor.createInstance(f.component);
    const auto& children = f.session.document().viewChildren(instance);
    REQUIRE(children.size() == 1);
    REQUIRE(f.session.document().sourceOf(children[0]) == f.badge);
    REQUIRE(f.session.document().ownerOf(children[0]) == instance);
    REQUIRE(f.session.document().box(instance).x == 240);
    REQUIRE(vc::worldBounds(f.session.document(), children[0])->x == 250);
}

TEST_CASE("instances follow edits to their component") {
    Fixture f;
    const Id instance = *f.editor.createInstance(f.component);
    vc::NodePatch blue;
    blue.fill = Oklch{0.6, 0.2, 250};
    f.session.edit(vc::SetProps{f.badge, blue});

    const Id copy = f.session.document().viewChildren(instance).at(0);
    REQUIRE(f.view(copy).fill == vc::Value<Oklch>{Oklch{0.6, 0.2, 250}});
}

TEST_CASE("style edits on an instance become overrides, geometry stays its own") {
    Fixture f;
    const Id instance = *f.editor.createInstance(f.component);
    (void)f.session.takeOutbox();

    vc::NodePatch patch;
    patch.x = 500.0;
    patch.fill = Oklch{0.3, 0.1, 30};
    patch.fillVisible = true;
    REQUIRE(f.editor.setProps(instance, patch));

    const Node& node = *f.session.document().find(instance);
    REQUIRE(node.x == 500);
    REQUIRE(node.overrides.size() == 1);
    REQUIRE(node.overrides[0].node == f.component);
    REQUIRE(f.view(instance).fill == vc::Value<Oklch>{Oklch{0.3, 0.1, 30}});
    REQUIRE(f.session.document().find(f.component)->fill != vc::Value<Oklch>{Oklch{0.3, 0.1, 30}});
    REQUIRE(f.session.takeOutbox().size() == 2);

    f.editor.undo();
    REQUIRE(f.session.document().find(instance)->overrides.empty());
}

TEST_CASE("overrides target nested component nodes and can be reset") {
    Fixture f;
    const Id instance = *f.editor.createInstance(f.component);
    vc::NodePatch hidden;
    hidden.visible = false;
    REQUIRE(f.session.edit(vc::SetOverride{instance, f.badge, hidden}));

    const Id copy = f.session.document().viewChildren(instance).at(0);
    REQUIRE_FALSE(f.view(copy).visible);
    REQUIRE(f.session.document().find(f.badge)->visible);

    REQUIRE(f.editor.resetOverrides(instance));
    REQUIRE(f.view(f.session.document().viewChildren(instance).at(0)).visible);
}

TEST_CASE("clicking inside an instance selects the instance") {
    Fixture f;
    const Id instance = *f.editor.createInstance(f.component);
    f.editor.clearSelection();
    f.editor.pointerDown({260, 20}, PointerButton::Primary);
    f.editor.pointerUp();
    REQUIRE(f.editor.selection() == std::vector<Id>{instance});
}

TEST_CASE("a component can't contain an instance of itself, even indirectly") {
    Fixture f;
    const Id instance = *f.editor.createInstance(f.component);
    REQUIRE_FALSE(f.session.edit(vc::MoveNode{instance, f.component, "z0"}));

    Node second;
    second.id = f.session.newId();
    second.type = NodeType::Component;
    second.order = "z1";
    second.w = second.h = 50;
    REQUIRE(f.session.edit(CreateNode{second}));
    Node inner;
    inner.id = f.session.newId();
    inner.type = NodeType::Instance;
    inner.component = second.id;
    inner.parent = f.component;
    inner.order = "z2";
    REQUIRE(f.session.edit(CreateNode{inner}));

    Node loop = inner;
    loop.id = f.session.newId();
    loop.component = f.component;
    loop.parent = second.id;
    REQUIRE_FALSE(f.session.edit(CreateNode{loop}));
}

TEST_CASE("a component in use can't be deleted or turned back into a frame") {
    Fixture f;
    f.editor.createInstance(f.component);
    vc::NodePatch frame;
    frame.type = NodeType::Frame;
    REQUIRE_FALSE(f.session.edit(vc::SetProps{f.component, frame}));

    REQUIRE(f.session.edit(vc::DeleteNode{f.badge}));
    REQUIRE_FALSE(f.session.edit(vc::DeleteNode{f.component}));
}

TEST_CASE("detaching turns an instance into ordinary nodes that look the same") {
    Fixture f;
    const Id instance = *f.editor.createInstance(f.component);
    vc::NodePatch hidden;
    hidden.visible = false;
    f.session.edit(vc::SetOverride{instance, f.badge, hidden});

    const Id frame = *f.editor.detachInstance(instance);
    const vc::Document& document = f.session.document();
    REQUIRE_FALSE(document.find(instance));
    REQUIRE(document.find(frame)->type == NodeType::Frame);
    const auto& children = document.children(frame);
    REQUIRE(children.size() == 1);
    REQUIRE_FALSE(document.find(children[0])->visible);
    REQUIRE(document.box(frame).x == 240);

    f.editor.undo();
    REQUIRE(document.find(instance));
}

TEST_CASE("wrapping a selection creates a component around it") {
    Session session;
    vc::Camera camera;
    Editor editor{session, camera};
    editor.setTool(Tool::Rect);
    editor.pointerDown({10, 10}, PointerButton::Primary);
    editor.pointerMove({50, 50});
    editor.pointerUp();
    const Id a = editor.selection().at(0);
    editor.setTool(Tool::Ellipse);
    editor.pointerDown({100, 20}, PointerButton::Primary);
    editor.pointerMove({140, 60});
    editor.pointerUp();
    const Id b = editor.selection().at(0);
    editor.setSelection({a, b});

    const Id component = *editor.createComponent();
    REQUIRE(session.document().find(component)->type == NodeType::Component);
    REQUIRE(session.document().box(component).x == 10);
    REQUIRE(session.document().box(component).w == 130);
    REQUIRE(session.document().find(a)->parent == component);
    REQUIRE(session.document().find(a)->x == 0);
    REQUIRE(session.document().find(b)->x == 90);

    editor.undo();
    REQUIRE_FALSE(session.document().find(component));
    REQUIRE(session.document().find(a)->parent.isNil());
}

TEST_CASE("components, instances and overrides survive a json round trip") {
    Fixture f;
    const Id instance = *f.editor.createInstance(f.component);
    vc::NodePatch hidden;
    hidden.visible = false;
    f.session.edit(vc::SetOverride{instance, f.badge, hidden});

    const auto ops = vc::snapshotFromJson(vc::snapshotToJson(f.session.document()));
    REQUIRE(ops.has_value());
    vc::Document copy;
    for (const vc::OpBody& op : *ops) REQUIRE(copy.apply(op));
    REQUIRE(*copy.find(instance) == *f.session.document().find(instance));
    REQUIRE(copy.find(f.component)->type == NodeType::Component);

    const vc::OpBody op = vc::SetOverride{instance, f.badge, hidden};
    REQUIRE(*vc::opFromJson(vc::opKind(op), vc::opToJson(op)) == op);
}

TEST_CASE("revealing a node out of view selects it and centers it") {
    Fixture f;
    f.camera.pan({-2000, 0});
    f.editor.reveal(f.component, {800, 600});
    REQUIRE(f.editor.selection() == std::vector<Id>{f.component});
    const vc::Vec2 center = f.camera.toScreen({100, 50});
    REQUIRE(center.x == 400);
    REQUIRE(center.y == 300);
}

TEST_CASE("double-clicking an instance selects the copy under the pointer, edits become overrides") {
    Fixture f;
    const Id instance = *f.editor.createInstance(f.component);
    const Id copy = f.session.document().viewChildren(instance).at(0);
    f.editor.doubleClick({260, 20});
    REQUIRE(f.editor.selection() == std::vector<Id>{copy});

    vc::NodePatch red;
    red.fill = Oklch{0.6, 0.2, 30};
    REQUIRE(f.editor.setProps(copy, red));
    REQUIRE(f.view(copy).fill == vc::Value<Oklch>{Oklch{0.6, 0.2, 30}});
    REQUIRE(f.session.document().find(f.badge)->fill != vc::Value<Oklch>{Oklch{0.6, 0.2, 30}});
    REQUIRE(f.session.document().find(instance)->overrides.at(0).node == f.badge);
    REQUIRE(f.editor.selection() == std::vector<Id>{copy});

    f.editor.pointerDown({260, 20}, PointerButton::Primary);
    f.editor.pointerUp();
    REQUIRE(f.editor.selection() == std::vector<Id>{copy});
    REQUIRE_FALSE(f.editor.deleteSelection());
    REQUIRE(f.session.document().find(f.badge));
}

TEST_CASE("a deep click selects the copy directly") {
    Fixture f;
    const Id instance = *f.editor.createInstance(f.component);
    f.editor.clearSelection();
    f.editor.pointerDown({260, 20}, PointerButton::Primary, {.deep = true});
    f.editor.pointerUp();
    REQUIRE(f.editor.selection() == std::vector<Id>{f.session.document().viewChildren(instance).at(0)});
}

TEST_CASE("overrides tell apart two instances of the same component nested in another") {
    Fixture f;
    Node card;
    card.id = f.session.newId();
    card.type = NodeType::Component;
    card.order = "z1";
    card.x = 0;
    card.y = 300;
    card.w = 500;
    card.h = 200;
    REQUIRE(f.session.edit(CreateNode{card}));
    Node first;
    first.id = f.session.newId();
    first.type = NodeType::Instance;
    first.component = f.component;
    first.parent = card.id;
    first.order = "a0";
    first.w = 200;
    first.h = 100;
    Node second = first;
    second.id = f.session.newId();
    second.order = "a1";
    second.x = 250;
    REQUIRE(f.session.edit(CreateNode{first}));
    REQUIRE(f.session.edit(CreateNode{second}));

    const Id outer = *f.editor.createInstance(card.id);
    const vc::Document& document = f.session.document();
    const std::vector<Id> nested = document.viewChildren(outer);
    REQUIRE(nested.size() == 2);
    const Id firstBadge = document.viewChildren(nested[0]).at(0);
    const Id secondBadge = document.viewChildren(nested[1]).at(0);

    vc::NodePatch hidden;
    hidden.visible = false;
    REQUIRE(f.editor.setProps(firstBadge, hidden));
    REQUIRE_FALSE(f.view(firstBadge).visible);
    REQUIRE(f.view(secondBadge).visible);

    vc::NodePatch moved;
    moved.x = 260.0;
    REQUIRE(f.editor.setProps(nested[1], moved));
    REQUIRE(f.view(nested[1]).x == 260);
    REQUIRE(document.find(second.id)->x == 250);
}
