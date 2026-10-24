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

namespace {
struct SwapFixture : Fixture {
    Id first, second, firstPart, secondPart, slot;
    vc::ComponentDefinition definition;
    SwapFixture() {
        const auto main = [&](const char* name, double x, Id& part) {
            Node n; n.id = session.newId(); n.type = NodeType::Component; n.name = name; n.order = "a1"; n.x = x; n.w = n.h = 32;
            REQUIRE(session.edit(CreateNode{n}));
            Node child; child.id = session.newId(); child.parent = n.id; child.order = "a0"; child.w = child.h = x == 350 ? 8 : 16;
            part = child.id; REQUIRE(session.edit(CreateNode{child}));
            return n.id;
        };
        first = main("First icon", 350, firstPart);
        second = main("Second icon", 450, secondPart);
        Node instance; instance.id = session.newId(); slot = instance.id; instance.type = NodeType::Instance;
        instance.component = first; instance.parent = component; instance.order = "a1"; instance.x = 80; instance.y = 10; instance.w = instance.h = 32;
        REQUIRE(session.edit(CreateNode{instance}));
        definition.swaps.push_back({"icon", slot, first, {first, second}});
        REQUIRE(editor.setComponentDefinition(component, definition));
    }
    Id copySlot(Id instance) { return session.document().viewChildren(instance).back(); }
    bool choose(Id instance, Id choice) {
        vc::NodePatch p; p.componentValues = vc::ComponentValues{{"icon", choice.toString()}};
        return editor.setProps(instance, p);
    }
};
}

TEST_CASE("declared instance swaps preserve linkage, geometry and snapshot semantics") {
    SwapFixture f;
    const Id one = *f.editor.createInstance(f.component), two = *f.editor.createInstance(f.component);
    const Id slot = f.copySlot(one);
    REQUIRE(f.view(slot).component == f.first);
    REQUIRE(f.choose(one, f.second));
    REQUIRE(f.view(slot).component == f.second);
    REQUIRE(f.view(slot).w == 32);
    REQUIRE(f.view(slot).x == 80);
    REQUIRE(f.session.document().sourceOf(f.session.document().viewChildren(slot).at(0)) == f.secondPart);
    REQUIRE(f.view(f.copySlot(two)).component == f.first);
    REQUIRE(f.session.document().find(f.slot)->component == f.first);
    REQUIRE(f.editor.undo()); REQUIRE(f.view(slot).component == f.first);
    REQUIRE(f.editor.redo()); REQUIRE(f.view(slot).component == f.second);
    const auto snapshot = vc::snapshotToJson(f.session.document());
    REQUIRE(snapshot.find("\"version\":4") != std::string::npos);
    auto ops = vc::snapshotFromJson(snapshot); REQUIRE(ops.has_value());
    vc::Document loaded; for (const auto& op : *ops) REQUIRE(loaded.apply(op));
    REQUIRE(loaded.find(f.component)->definition == f.definition);
    REQUIRE(loaded.view(slot)->component == f.second);
    auto legacy = snapshot; legacy.replace(legacy.find("\"version\":4"), 11, "\"version\":3");
    REQUIRE_FALSE(vc::snapshotFromJson(legacy).has_value());
    REQUIRE(*vc::opFromJson("SetComponentDefinition", vc::opToJson(vc::SetComponentDefinition{f.component, f.definition})) == vc::OpBody{vc::SetComponentDefinition{f.component, f.definition}});
    REQUIRE(vc::inspectNodeJson(f.session.document(), *f.session.document().view(slot), vc::kDefaultModeId).find("Second icon") != std::string::npos);
}

TEST_CASE("swap choices participate in cycle, deletion, target and value validation") {
    SwapFixture f;
    const Id instance = *f.editor.createInstance(f.component);
    REQUIRE_FALSE(f.choose(instance, f.component));
    auto bad = f.definition; bad.swaps[0].choices.push_back(f.component);
    REQUIRE_FALSE(f.editor.setComponentDefinition(f.component, bad));
    bad = f.definition; bad.swaps[0].choices = {f.first, f.first};
    REQUIRE_FALSE(f.editor.setComponentDefinition(f.component, bad));
    bad = f.definition; bad.swaps[0].target = f.badge;
    REQUIRE_FALSE(f.editor.setComponentDefinition(f.component, bad));
    bad = f.definition; bad.swaps[0].defaultComponent = f.component;
    REQUIRE_FALSE(f.editor.setComponentDefinition(f.component, bad));
    REQUIRE_FALSE(f.session.edit(vc::DeleteNode{f.second}));
    REQUIRE_FALSE(f.session.deleteTree(f.second));
    REQUIRE(f.session.document().find(f.secondPart));
    vc::NodePatch demote; demote.type = NodeType::Frame;
    REQUIRE_FALSE(f.editor.setProps(f.second, demote));
    Node recursive; recursive.id = f.session.newId(); recursive.type = NodeType::Instance; recursive.component = f.component; recursive.parent = f.second; recursive.order = "a1";
    REQUIRE_FALSE(f.session.edit(CreateNode{recursive}));
    REQUIRE(f.choose(instance, f.second));
    bad = f.definition; bad.swaps[0].choices = {f.first};
    REQUIRE_FALSE(f.editor.setComponentDefinition(f.component, bad));
    vc::NodePatch direct; direct.component = f.second;
    REQUIRE_FALSE(f.editor.setProps(f.slot, direct));
    vc::NodePatch prop; prop.componentValues = vc::ComponentValues{{"icon", true}};
    REQUIRE_FALSE(f.editor.setProps(instance, prop));
    REQUIRE_FALSE(f.session.edit(vc::MoveNode{f.slot, {}, "a0"}));
}

TEST_CASE("swaps refuse affected overrides while preserving sibling and root paint") {
    SwapFixture f;
    const Id instance = *f.editor.createInstance(f.component), slot = f.copySlot(instance);
    const Id copiedPart = f.session.document().viewChildren(slot).at(0);
    const Id sibling = f.session.document().viewChildren(instance).front();
    vc::NodePatch paint; paint.opacity = 0.4;
    REQUIRE(f.editor.setProps(copiedPart, paint));
    REQUIRE(f.editor.setProps(sibling, paint));
    vc::NodePatch wrapperPaint; wrapperPaint.opacity = 0.7;
    REQUIRE(f.editor.setProps(slot, wrapperPaint));
    REQUIRE_FALSE(f.choose(instance, f.second));
    REQUIRE(f.view(slot).component == f.first);
    REQUIRE(f.editor.resetOverrides(copiedPart));
    REQUIRE(f.choose(instance, f.second));
    REQUIRE(f.view(sibling).opacity == vc::Value<double>{0.4});
    REQUIRE(f.session.document().find(instance)->overrides.size() == 2);
    REQUIRE(f.view(slot).opacity == vc::Value<double>{0.7});
    REQUIRE(f.editor.setProps(f.slot, paint)); // Common root paint is transferable.
    REQUIRE(f.view(slot).opacity == vc::Value<double>{0.7});
    REQUIRE(f.view(f.slot).opacity == vc::Value<double>{0.4});
    REQUIRE_FALSE(f.session.edit(vc::SetOverride{f.slot, f.firstPart, paint}));
    const Id unknown = f.session.newId();
    REQUIRE(f.session.edit(vc::SetOverride{instance, unknown, paint}));
    REQUIRE_FALSE(f.choose(instance, f.first));
    REQUIRE(f.session.edit(vc::SetOverride{instance, unknown, {}}));
    const auto snapshot = vc::snapshotToJson(f.session.document());
    auto ops = vc::snapshotFromJson(snapshot); REQUIRE(ops.has_value());
    vc::Document loaded; for (const auto& op : *ops) REQUIRE(loaded.apply(op));
    REQUIRE(loaded.view(slot)->component == f.second);
    REQUIRE(loaded.view(slot)->opacity == vc::Value<double>{0.7});
    REQUIRE(loaded.view(sibling)->opacity == vc::Value<double>{0.4});
    auto changed = f.definition; changed.swaps[0].defaultComponent = f.second;
    REQUIRE_FALSE(f.editor.setComponentDefinition(f.component, changed));
}

TEST_CASE("bound detach fails atomically and deleting a swapped instance remains undoable") {
    SwapFixture f;
    const Id instance = *f.editor.createInstance(f.component);
    REQUIRE(f.choose(instance, f.second));
    REQUIRE_FALSE(f.editor.detachInstance(f.slot).has_value());
    REQUIRE(f.view(f.copySlot(instance)).component == f.second);
    REQUIRE(f.editor.undo());
    REQUIRE(f.view(f.copySlot(instance)).component == f.first);
    REQUIRE(f.choose(instance, f.second));
    const Id child = f.session.document().viewChildren(f.copySlot(instance)).at(0);
    vc::NodePatch paint; paint.opacity = 0.5;
    REQUIRE(f.editor.setProps(child, paint));
    REQUIRE(f.session.deleteTree(instance));
    REQUIRE_FALSE(f.session.document().find(instance));
    REQUIRE(f.editor.undo());
    REQUIRE(f.view(child).opacity == vc::Value<double>{0.5});
    REQUIRE(f.view(f.copySlot(instance)).component == f.second);
}

TEST_CASE("a declared swap cannot strand overrides held by an enclosing instance") {
    SwapFixture f;
    Node outer; outer.id = f.session.newId(); outer.type = NodeType::Component; outer.order = "a2"; outer.w = 200; outer.h = 100;
    REQUIRE(f.session.edit(CreateNode{outer}));
    Node host; host.id = f.session.newId(); host.type = NodeType::Instance; host.component = f.component; host.parent = outer.id; host.order = "a0"; host.w = 200; host.h = 100;
    REQUIRE(f.session.edit(CreateNode{host}));
    const Id instance = *f.editor.createInstance(outer.id);
    const Id nested = f.session.document().viewChildren(instance).at(0);
    const Id slot = f.session.document().viewChildren(nested).back();
    const Id part = f.session.document().viewChildren(slot).at(0);
    vc::NodePatch paint; paint.opacity = 0.5;
    REQUIRE(f.editor.setProps(part, paint));
    REQUIRE_FALSE(f.choose(host.id, f.second));
    REQUIRE(f.view(slot).component == f.first);
    REQUIRE(f.editor.resetOverrides(part));
    REQUIRE(f.choose(host.id, f.second));
    REQUIRE(f.view(slot).component == f.second);
}

TEST_CASE("an actual nested instance addresses overrides independently of its main's definition layers") {
    SwapFixture f;
    REQUIRE(f.editor.setComponentDefinition(f.component, {}));
    const Id copy = f.session.document().viewChildren(f.slot).at(0);
    vc::NodePatch paint; paint.opacity = 0.3;
    REQUIRE(f.editor.setProps(copy, paint));
    REQUIRE(f.view(copy).opacity == vc::Value<double>{0.3});
    REQUIRE(f.session.document().find(f.slot)->overrides.at(0).node == f.firstPart);
    REQUIRE(f.editor.undo());
    REQUIRE(f.view(copy).opacity == vc::Value<double>{1.0});
}

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

TEST_CASE("declared boolean properties and multi-axis variants resolve without detaching instances") {
    Fixture f;
    vc::ComponentDefinition d;
    d.properties.push_back({"show-badge", f.badge, false});
    d.axes.push_back({"size", {"compact", "large"}, "compact"});
    d.axes.push_back({"tone", {"neutral", "danger"}, "neutral"});
    vc::NodePatch wide; wide.w = 90;
    vc::NodePatch red; red.fill = Oklch{0.5, 0.2, 25};
    vc::NodePatch rounded; rounded.cornerRadius = 12.0;
    d.rules.push_back({{{"size", "large"}}, f.badge, wide});
    d.rules.push_back({{{"tone", "danger"}}, f.badge, red});
    d.rules.push_back({{{"size", "large"}, {"tone", "danger"}}, f.badge, rounded});
    REQUIRE(f.editor.setComponentDefinition(f.component, d));
    REQUIRE_FALSE(f.view(f.badge).visible);
    const Id instance = *f.editor.createInstance(f.component);
    const Id copy = f.session.document().viewChildren(instance).at(0);
    vc::NodePatch values;
    values.componentValues = vc::ComponentValues{{"show-badge", true}, {"size", std::string{"large"}}, {"tone", std::string{"danger"}}};
    REQUIRE(f.editor.setProps(instance, values));
    REQUIRE(f.view(copy).visible);
    REQUIRE(f.view(copy).w == 90);
    REQUIRE(f.view(copy).fill == *red.fill);
    REQUIRE(f.view(copy).cornerRadius == vc::Value<double>{12.0});
    REQUIRE(f.session.document().find(instance)->component == f.component);
    REQUIRE(f.session.document().find(instance)->overrides.empty());
    REQUIRE_FALSE(f.view(f.badge).visible);
    vc::NodePatch local; local.w = 110;
    REQUIRE(f.editor.setProps(copy, local));
    values.componentValues = vc::ComponentValues{{"show-badge", true}, {"size", std::string{"compact"}}};
    REQUIRE(f.editor.setProps(instance, values));
    REQUIRE(f.view(copy).w == 110);
    REQUIRE(f.editor.undo());
    REQUIRE(std::get<std::string>(f.session.document().find(instance)->componentValues.at("size")) == "large");
    REQUIRE(f.editor.redo());
    REQUIRE(f.view(copy).w == 110);
    const std::string snapshot = vc::snapshotToJson(f.session.document());
    REQUIRE(snapshot.find("\"version\":3") != std::string::npos);
    const auto ops = vc::snapshotFromJson(snapshot);
    REQUIRE(ops.has_value());
    vc::Document loaded;
    for (const auto& op : *ops) REQUIRE(loaded.apply(op));
    REQUIRE(loaded.find(f.component)->definition == d);
    REQUIRE(loaded.find(instance)->componentValues == f.session.document().find(instance)->componentValues);
    REQUIRE(loaded.view(copy)->w == 110);
    const vc::OpBody definitionOp = vc::SetComponentDefinition{f.component, d};
    REQUIRE(*vc::opFromJson(vc::opKind(definitionOp), vc::opToJson(definitionOp)) == definitionOp);
    std::string legacy = snapshot;
    legacy.replace(legacy.find("\"version\":3"), 11, "\"version\":2");
    REQUIRE_FALSE(vc::snapshotFromJson(legacy).has_value());
}

TEST_CASE("component definitions reject invalid targets, defaults, values and destructive schema edits") {
    Fixture f;
    vc::ComponentDefinition d;
    d.properties.push_back({"show-badge", f.badge, true});
    d.axes.push_back({"size", {"compact", "large"}, "compact"});
    REQUIRE(f.editor.setComponentDefinition(f.component, d));
    const Id instance = *f.editor.createInstance(f.component);
    vc::NodePatch values; values.componentValues = vc::ComponentValues{{"size", std::string{"large"}}};
    REQUIRE(f.editor.setProps(instance, values));
    auto invalid = d; invalid.axes[0].values = {"compact"};
    REQUIRE_FALSE(f.editor.setComponentDefinition(f.component, invalid));
    invalid = d; invalid.properties[0].target = instance;
    REQUIRE_FALSE(f.editor.setComponentDefinition(f.component, invalid));
    invalid = d; invalid.axes[0].defaultValue = "missing";
    REQUIRE_FALSE(f.editor.setComponentDefinition(f.component, invalid));
    values.componentValues = vc::ComponentValues{{"show-badge", std::string{"false"}}};
    REQUIRE_FALSE(f.editor.setProps(instance, values));
    values.componentValues = vc::ComponentValues{{"unknown", true}};
    REQUIRE_FALSE(f.editor.setProps(instance, values));
    REQUIRE_FALSE(f.session.edit(vc::DeleteNode{f.badge}));
    Node group;
    group.id = f.session.newId(); group.type = NodeType::Frame; group.parent = f.component; group.order = "a1";
    REQUIRE(f.session.edit(CreateNode{group}));
    REQUIRE(f.session.edit(vc::MoveNode{f.badge, group.id, "a0"}));
    REQUIRE_FALSE(f.session.edit(vc::MoveNode{f.badge, {}, "a0"}));
    REQUIRE(f.session.document().find(f.badge)->parent == group.id);
    REQUIRE_FALSE(f.session.deleteTree(f.component));
    REQUIRE(f.session.document().find(f.badge));
    REQUIRE(f.session.document().find(f.component)->definition == d);
    REQUIRE(f.session.deleteTree(instance));
    REQUIRE(f.session.deleteTree(f.component));
    REQUIRE(f.editor.undo());
    REQUIRE(f.session.document().find(f.component)->definition == d);
    REQUIRE(f.session.document().find(f.badge));
}

TEST_CASE("editing axis choices retains explicit choices and styles through undo and reload") {
    Fixture f;
    vc::ComponentDefinition definition;
    definition.axes = {{"size", {"compact", "large"}, "compact"}};
    vc::NodePatch wide; wide.w = 90;
    definition.rules = {{{{"size", "large"}}, f.badge, wide}};
    REQUIRE(f.editor.setComponentDefinition(f.component, definition));
    const Id inherited = *f.editor.createInstance(f.component);
    const Id explicitChoice = *f.editor.createInstance(f.component);
    vc::NodePatch values; values.componentValues = vc::ComponentValues{{"size", std::string{"compact"}}};
    REQUIRE(f.editor.setProps(explicitChoice, values));
    auto edited = definition;
    edited.axes[0].values = {"large", "compact", "roomy"};
    edited.axes[0].defaultValue = "large";
    REQUIRE(f.editor.setComponentDefinition(f.component, edited));
    const Id inheritedChild = f.session.document().viewChildren(inherited).at(0);
    const Id explicitChild = f.session.document().viewChildren(explicitChoice).at(0);
    REQUIRE(f.view(inheritedChild).w == 90);
    REQUIRE(f.view(explicitChild).w != 90);
    const auto saved = vc::snapshotToJson(f.session.document());
    auto invalid = edited; invalid.axes[0].values = {"large", "roomy"};
    REQUIRE_FALSE(f.editor.setComponentDefinition(f.component, invalid));
    invalid = edited; invalid.axes[0].values = {"compact", "roomy"}; invalid.axes[0].defaultValue = "compact";
    REQUIRE_FALSE(f.editor.setComponentDefinition(f.component, invalid));
    REQUIRE(vc::snapshotToJson(f.session.document()) == saved);
    REQUIRE(f.editor.undo());
    REQUIRE(f.session.document().find(f.component)->definition == definition);
    REQUIRE(f.editor.redo());
    REQUIRE(vc::snapshotToJson(f.session.document()) == saved);
    const auto ops = vc::snapshotFromJson(saved); REQUIRE(ops.has_value());
    vc::Document loaded; for (const auto& op : *ops) REQUIRE(loaded.apply(op));
    REQUIRE(loaded.find(f.component)->definition == edited);
    REQUIRE(std::get<std::string>(loaded.find(explicitChoice)->componentValues.at("size")) == "compact");
    REQUIRE(loaded.view(inheritedChild)->w == 90);
    edited.axes[0].values = {"large", "compact"};
    REQUIRE(f.editor.setComponentDefinition(f.component, edited));
    REQUIRE(f.session.document().find(f.component)->definition.axes[0].values.size() == 2);
}

TEST_CASE("variant specificity precedes local overrides and detaching preserves the resolved result") {
    Fixture f;
    vc::ComponentDefinition definition;
    definition.axes = {{"size", {"small", "large"}, "small"}, {"tone", {"neutral", "danger"}, "neutral"}};
    vc::NodePatch specific; specific.cornerRadius = 12.0;
    vc::NodePatch general; general.cornerRadius = 4.0;
    // Deliberately authored in reverse specificity order.
    definition.rules = {{{{"size", "large"}, {"tone", "danger"}}, f.badge, specific}, {{{"size", "large"}}, f.badge, general}};
    REQUIRE(f.editor.setComponentDefinition(f.component, definition));
    const Id instance = *f.editor.createInstance(f.component);
    vc::NodePatch choices; choices.componentValues = vc::ComponentValues{{"size", std::string{"large"}}, {"tone", std::string{"danger"}}};
    REQUIRE(f.editor.setProps(instance, choices));
    const Id child = f.session.document().viewChildren(instance).at(0);
    REQUIRE(f.view(child).cornerRadius == vc::Value<double>{12.0});
    vc::NodePatch local; local.cornerRadius = 20.0;
    REQUIRE(f.editor.setProps(child, local));
    REQUIRE(f.view(child).cornerRadius == vc::Value<double>{20.0});
    const Id detached = *f.editor.detachInstance(instance);
    REQUIRE(f.session.document().find(detached)->type == NodeType::Frame);
    REQUIRE(f.session.document().find(detached)->definition.empty());
    REQUIRE(f.session.document().find(detached)->componentValues.empty());
    const Id detachedChild = f.session.document().children(detached).at(0);
    REQUIRE(f.view(detachedChild).cornerRadius == vc::Value<double>{20.0});
    REQUIRE(f.editor.undo());
    REQUIRE(f.session.document().find(instance)->componentValues == *choices.componentValues);
    REQUIRE(f.view(child).cornerRadius == vc::Value<double>{20.0});
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

TEST_CASE("resetting one copied layer preserves the instance root and sibling instance overrides") {
    Fixture f;
    const Id first = *f.editor.createInstance(f.component);
    const Id a = f.session.document().viewChildren(first).at(0);
    const Id second = *f.editor.createInstance(f.component);
    const Id b = f.session.document().viewChildren(second).at(0);
    vc::NodePatch red;
    red.fill = Oklch{0.5, 0.2, 25};
    vc::NodePatch blue;
    blue.fill = Oklch{0.5, 0.2, 250};
    REQUIRE(f.editor.setProps(a, red));
    REQUIRE(f.editor.setProps(b, blue));
    vc::NodePatch root;
    root.clipContent = true;
    REQUIRE(f.editor.setProps(first, root));
    REQUIRE(vc::inspectNodeJson(f.session.document(), f.view(a), {}).find("\"overrides\":1") != std::string::npos);
    REQUIRE(f.editor.resetOverrides(a));
    REQUIRE(f.view(a).fill == f.session.document().find(f.badge)->fill);
    REQUIRE(f.view(b).fill == *blue.fill);
    REQUIRE(f.view(first).clipContent);
    REQUIRE(f.session.document().find(first)->overrides.size() == 1);
    REQUIRE(vc::inspectNodeJson(f.session.document(), f.view(a), {}).find("\"overrides\":0") != std::string::npos);
    REQUIRE_FALSE(f.editor.resetOverrides(a));
    REQUIRE(f.editor.undo());
    REQUIRE(f.view(a).fill == *red.fill);
    REQUIRE(f.view(b).fill == *blue.fill);
    REQUIRE(f.editor.redo());
    REQUIRE(f.view(a).fill == f.session.document().find(f.badge)->fill);
}

TEST_CASE("switching a clean instance preserves geometry and is undoable") {
    Fixture f;
    const Id instance = *f.editor.createInstance(f.component);
    Node other;
    other.id = f.session.newId();
    other.type = NodeType::Component;
    other.order = "z1";
    other.w = 80;
    other.h = 40;
    REQUIRE(f.session.edit(CreateNode{other}));
    const Node before = *f.session.document().find(instance);
    vc::NodePatch patch;
    patch.component = other.id;
    const auto structure = f.session.structureRevision();
    REQUIRE(f.editor.setProps(instance, patch));
    REQUIRE(f.session.structureRevision() > structure);
    REQUIRE(f.session.document().find(instance)->component == other.id);
    REQUIRE(f.session.document().find(instance)->x == before.x);
    REQUIRE(f.session.document().find(instance)->w == before.w);
    REQUIRE(f.editor.undo());
    REQUIRE(*f.session.document().find(instance) == before);
    REQUIRE(f.editor.redo());
    REQUIRE(f.session.document().find(instance)->component == other.id);
    vc::Document loaded;
    const auto ops = vc::snapshotFromJson(vc::snapshotToJson(f.session.document()));
    REQUIRE(ops.has_value());
    for (const vc::OpBody& op : *ops) REQUIRE(loaded.apply(op));
    REQUIRE(loaded.find(instance)->component == other.id);
}

TEST_CASE("switching a component refuses cycles, missing mains and unreviewed overrides") {
    Fixture f;
    const Id instance = *f.editor.createInstance(f.component);
    vc::NodePatch patch;
    patch.component = f.session.newId();
    REQUIRE_FALSE(f.editor.setProps(instance, patch));
    patch.component = f.badge;
    REQUIRE_FALSE(f.editor.setProps(instance, patch));
    Node other;
    other.id = f.session.newId();
    other.type = NodeType::Component;
    other.order = "z1";
    REQUIRE(f.session.edit(CreateNode{other}));
    vc::NodePatch hidden;
    hidden.visible = false;
    REQUIRE(f.session.edit(vc::SetOverride{instance, f.badge, hidden}));
    const Node before = *f.session.document().find(instance);
    patch.component = other.id;
    patch.fill = Oklch{0.5, 0.1, 180};
    REQUIRE_FALSE(f.editor.setProps(instance, patch));
    REQUIRE(*f.session.document().find(instance) == before);
    REQUIRE(f.editor.resetOverrides(instance));
    REQUIRE(f.editor.setProps(instance, patch));
    REQUIRE(f.session.document().find(instance)->overrides.at(0).node == other.id);
    Node nested;
    nested.id = f.session.newId();
    nested.type = NodeType::Instance;
    nested.component = other.id;
    nested.parent = f.component;
    nested.order = "z1";
    REQUIRE(f.session.edit(CreateNode{nested}));
    patch.component = f.component;
    REQUIRE_FALSE(f.editor.setProps(nested.id, patch));
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
    REQUIRE(f.editor.resetOverrides(firstBadge));
    REQUIRE(f.view(firstBadge).visible);
    REQUIRE(f.view(secondBadge).visible);
    REQUIRE(f.view(nested[1]).x == 260);
    REQUIRE(f.editor.undo());
    REQUIRE_FALSE(f.view(firstBadge).visible);
    REQUIRE(f.view(nested[1]).x == 260);
}
