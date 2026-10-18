#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include "vc/editor.hpp"
#include "vc/geometry.hpp"

using vc::Camera;
using vc::Editor;
using vc::Node;
using vc::NodeType;
using vc::PointerButton;
using vc::Session;
using vc::Tool;

namespace {

struct Fixture {
    Session session{vc::IdGenerator{11}};
    Camera camera;
    Editor editor{session, camera};

    const Node& selected() const { return *session.document().find(editor.selection().at(0)); }

    void drag(vc::Vec2 from, vc::Vec2 to, PointerButton button = PointerButton::Primary) {
        editor.pointerDown(from, button);
        editor.pointerMove({(from.x + to.x) / 2, (from.y + to.y) / 2});
        editor.pointerMove(to);
        editor.pointerUp();
    }
};

} // namespace

TEST_CASE("dragging with a draw tool creates one shape as a single op") {
    Fixture f;
    f.editor.setTool(Tool::Rect);
    f.drag({50, 40}, {10, 100});

    REQUIRE(f.session.document().size() == 1);
    const Node& rect = f.selected();
    REQUIRE(rect.type == NodeType::Rect);
    REQUIRE(rect.x == 10);
    REQUIRE(rect.y == 40);
    REQUIRE(rect.w == 40);
    REQUIRE(rect.h == 60);
    REQUIRE(f.session.takeOutbox().size() == 1);
    REQUIRE(f.editor.tool() == Tool::Select);
}

TEST_CASE("clicking with a draw tool creates a default-sized shape") {
    Fixture f;
    f.editor.setTool(Tool::Ellipse);
    f.editor.pointerDown({20, 30}, PointerButton::Primary);
    f.editor.pointerUp();

    const Node& ellipse = f.selected();
    REQUIRE(ellipse.type == NodeType::Ellipse);
    REQUIRE(ellipse.x == 20);
    REQUIRE(ellipse.w == Editor::kDefaultShapeSize);
}

TEST_CASE("shapes drawn inside a frame become its children, positioned relative to it") {
    Fixture f;
    f.editor.setTool(Tool::Frame);
    f.drag({100, 100}, {400, 400});
    const vc::Id frame = f.editor.selection().at(0);

    f.editor.setTool(Tool::Rect);
    f.drag({150, 120}, {200, 170});

    const Node& rect = f.selected();
    REQUIRE(rect.parent == frame);
    REQUIRE(rect.x == 50);
    REQUIRE(rect.y == 20);
}

TEST_CASE("dragging a shape moves it with one op, and undo puts it back") {
    Fixture f;
    f.editor.setTool(Tool::Rect);
    f.drag({0, 0}, {100, 100});
    (void)f.session.takeOutbox();

    f.drag({50, 50}, {80, 20});
    REQUIRE(f.selected().x == 30);
    REQUIRE(f.selected().y == -30);
    REQUIRE(f.session.takeOutbox().size() == 1);

    REQUIRE(f.editor.undo());
    REQUIRE(f.selected().x == 0);
}

TEST_CASE("a click without movement selects without editing") {
    Fixture f;
    f.editor.setTool(Tool::Rect);
    f.drag({0, 0}, {100, 100});
    f.editor.clearSelection();
    (void)f.session.takeOutbox();

    f.editor.pointerDown({50, 50}, PointerButton::Primary);
    f.editor.pointerMove({51, 50});
    f.editor.pointerUp();

    REQUIRE(f.editor.selection().size() == 1);
    REQUIRE(f.session.takeOutbox().empty());
}

TEST_CASE("clicking empty canvas clears the selection") {
    Fixture f;
    f.editor.setTool(Tool::Rect);
    f.drag({0, 0}, {100, 100});

    f.editor.pointerDown({500, 500}, PointerButton::Primary);
    f.editor.pointerUp();

    REQUIRE(f.editor.selection().empty());
}

TEST_CASE("the middle button pans with any tool and never edits") {
    Fixture f;
    f.editor.setTool(Tool::Rect);
    f.drag({0, 0}, {40, 30}, PointerButton::Middle);

    REQUIRE(f.session.document().size() == 0);
    REQUIRE(f.camera.position().x == -40);
    REQUIRE(f.camera.position().y == -30);
}

TEST_CASE("deleting the selection removes the subtree and undo restores it") {
    Fixture f;
    f.editor.setTool(Tool::Frame);
    f.drag({0, 0}, {300, 300});
    const vc::Id frame = f.editor.selection().at(0);
    f.editor.setTool(Tool::Rect);
    f.drag({10, 10}, {50, 50});
    f.editor.select(frame);

    REQUIRE(f.editor.deleteSelection());
    REQUIRE(f.session.document().size() == 0);
    REQUIRE(f.editor.selection().empty());

    REQUIRE(f.editor.undo());
    REQUIRE(f.session.document().size() == 2);
}

TEST_CASE("undoing a creation drops the shape from the selection") {
    Fixture f;
    f.editor.setTool(Tool::Rect);
    f.drag({0, 0}, {10, 10});

    f.editor.undo();
    REQUIRE(f.editor.selection().empty());
}

TEST_CASE("hit testing respects frame clipping and paint order") {
    Fixture f;
    f.editor.setTool(Tool::Frame);
    f.drag({0, 0}, {100, 100});
    f.editor.setTool(Tool::Rect);
    f.drag({50, 50}, {200, 200});
    const vc::Id rect = f.editor.selection().at(0);

    REQUIRE(vc::hitTest(f.session.document(), {60, 60}) == rect);
    REQUIRE(vc::hitTest(f.session.document(), {150, 150}).isNil());
}

TEST_CASE("tool names match the client's") {
    REQUIRE(vc::toolFromName("rectangle") == Tool::Rect);
    REQUIRE(vc::toolFromName("hand") == Tool::Hand);
    REQUIRE_FALSE(vc::toolFromName("lasso"));
}

TEST_CASE("drawing at a fractional zoom snaps edges and sizes to whole units") {
    Fixture f;
    f.camera.zoomAt({0, 0}, 1.568);
    f.editor.setTool(Tool::Rect);
    f.drag({101, 57}, {333, 211});

    const Node& rect = f.selected();
    for (double v : {rect.x, rect.y, rect.w, rect.h}) {
        REQUIRE(v == std::round(v));
    }
}

TEST_CASE("a drag shorter than a unit still makes a one-unit shape") {
    Fixture f;
    f.camera.zoomAt({0, 0}, 20);
    f.editor.setTool(Tool::Rect);
    f.drag({0, 0}, {6, 4});

    REQUIRE(f.selected().w == 1);
    REQUIRE(f.selected().h == 1);
}

TEST_CASE("moving at a fractional zoom lands on whole units") {
    Fixture f;
    f.editor.setTool(Tool::Rect);
    f.drag({0, 0}, {100, 100});
    f.camera.zoomAt({0, 0}, 1.37);

    f.drag({50, 50}, {93, 71});
    REQUIRE(f.selected().x == std::round(f.selected().x));
    REQUIRE(f.selected().y == std::round(f.selected().y));
    REQUIRE(f.selected().x != 0);
}

TEST_CASE("snapping is absolute, so children of an off-grid frame get fractional offsets") {
    Fixture f;
    f.editor.setTool(Tool::Frame);
    f.drag({0, 0}, {300, 300});
    vc::NodePatch offGrid;
    offGrid.x = 10.25;
    f.session.edit(vc::SetProps{f.editor.selection().at(0), offGrid});

    f.editor.setTool(Tool::Rect);
    f.drag({50, 50}, {120, 120});

    const auto bounds = *vc::worldBounds(f.session.document(), f.editor.selection().at(0));
    REQUIRE(bounds.x == 50);
    REQUIRE(f.selected().x == 39.75);
}

TEST_CASE("without pixel snap, positions follow the pointer exactly") {
    Fixture f;
    f.editor.setPixelSnap(false);
    f.camera.zoomAt({0, 0}, 2);
    f.editor.setTool(Tool::Rect);
    f.drag({1, 1}, {31, 21});

    REQUIRE(f.selected().x == 0.5);
    REQUIRE(f.selected().w == 15);
}

TEST_CASE("shift draws a square in the drag direction") {
    Fixture f;
    f.editor.setTool(Tool::Rect);
    f.editor.pointerDown({100, 100}, PointerButton::Primary, {.shift = true});
    f.editor.pointerMove({40, 80}, {.shift = true});
    f.editor.pointerUp();

    const Node& rect = f.selected();
    REQUIRE(rect.w == 60);
    REQUIRE(rect.h == 60);
    REQUIRE(rect.x == 40);
    REQUIRE(rect.y == 40);
}

TEST_CASE("alt draws from the center") {
    Fixture f;
    f.editor.setTool(Tool::Ellipse);
    f.editor.pointerDown({100, 100}, PointerButton::Primary);
    f.editor.pointerMove({130, 120}, {.alt = true});
    f.editor.pointerUp();

    const Node& ellipse = f.selected();
    REQUIRE(ellipse.x == 70);
    REQUIRE(ellipse.y == 80);
    REQUIRE(ellipse.w == 60);
    REQUIRE(ellipse.h == 40);
}

TEST_CASE("pressing a modifier mid-gesture updates the shape without moving the pointer") {
    Fixture f;
    f.editor.setTool(Tool::Rect);
    f.editor.pointerDown({0, 0}, PointerButton::Primary);
    f.editor.pointerMove({80, 30});
    f.editor.setModifiers({.shift = true});
    f.editor.pointerUp();

    REQUIRE(f.selected().h == 80);
}

TEST_CASE("shift while moving locks to the dominant axis") {
    Fixture f;
    f.editor.setTool(Tool::Rect);
    f.drag({0, 0}, {100, 100});

    f.editor.pointerDown({50, 50}, PointerButton::Primary);
    f.editor.pointerMove({90, 62}, {.shift = true});
    f.editor.pointerUp();

    REQUIRE(f.selected().x == 40);
    REQUIRE(f.selected().y == 0);
}

TEST_CASE("nudging moves the selection by the delta as one undo step") {
    Fixture f;
    f.editor.setTool(Tool::Rect);
    f.drag({0, 0}, {100, 100});
    (void)f.session.takeOutbox();

    REQUIRE(f.editor.nudge({10, 0}));
    REQUIRE(f.editor.nudge({0, -1}));
    REQUIRE(f.selected().x == 10);
    REQUIRE(f.selected().y == -1);
    REQUIRE(f.session.takeOutbox().size() == 2);

    f.editor.undo();
    REQUIRE(f.selected().y == 0);
}

TEST_CASE("nudging with nothing selected does nothing") {
    Fixture f;
    REQUIRE_FALSE(f.editor.nudge({1, 0}));
}

namespace {

Fixture withRect() {
    Fixture f;
    f.editor.setTool(Tool::Rect);
    f.drag({100, 100}, {200, 150});
    return f;
}

} // namespace

TEST_CASE("handles are found at corners and along edges of the single selection") {
    Fixture f = withRect();

    REQUIRE(f.editor.handleAt({100, 100}) == vc::Handle{-1, -1});
    REQUIRE(f.editor.handleAt({203, 152}) == vc::Handle{1, 1});
    REQUIRE(f.editor.handleAt({150, 101}) == vc::Handle{0, -1});
    REQUIRE(f.editor.handleAt({198, 125}) == vc::Handle{1, 0});
    REQUIRE(f.editor.handleAt({150, 125}).isNone());

    f.editor.clearSelection();
    REQUIRE(f.editor.handleAt({100, 100}).isNone());
}

TEST_CASE("dragging the bottom-right corner resizes with one op and undoes") {
    Fixture f = withRect();
    (void)f.session.takeOutbox();

    f.drag({200, 150}, {260, 190});
    REQUIRE(f.selected().x == 100);
    REQUIRE(f.selected().w == 160);
    REQUIRE(f.selected().h == 90);
    REQUIRE(f.session.takeOutbox().size() == 1);

    f.editor.undo();
    REQUIRE(f.selected().w == 100);
}

TEST_CASE("dragging the left edge moves only the left side") {
    Fixture f = withRect();
    f.drag({100, 125}, {70, 140});

    REQUIRE(f.selected().x == 70);
    REQUIRE(f.selected().w == 130);
    REQUIRE(f.selected().y == 100);
    REQUIRE(f.selected().h == 50);
}

TEST_CASE("dragging past the opposite edge flips instead of going negative") {
    Fixture f = withRect();
    f.drag({200, 125}, {60, 125});

    REQUIRE(f.selected().x == 60);
    REQUIRE(f.selected().w == 40);
}

TEST_CASE("shift keeps proportions on a corner, alt resizes from the center") {
    Fixture f = withRect();
    f.editor.pointerDown({200, 150}, PointerButton::Primary);
    f.editor.pointerMove({300, 160}, {.shift = true});
    f.editor.pointerUp();
    REQUIRE(f.selected().w == 200);
    REQUIRE(f.selected().h == 100);

    Fixture g = withRect();
    g.editor.pointerDown({200, 125}, PointerButton::Primary);
    g.editor.pointerMove({210, 125}, {.alt = true});
    g.editor.pointerUp();
    REQUIRE(g.selected().x == 90);
    REQUIRE(g.selected().w == 120);
}

TEST_CASE("resizing snaps only the edges that move") {
    Fixture f = withRect();
    vc::NodePatch offGrid;
    offGrid.x = 100.5;
    f.editor.setProps(f.editor.selection().at(0), offGrid);

    f.camera.zoomAt({0, 0}, 1.3);
    const vc::Vec2 right = f.camera.toScreen({200.5, 125});
    f.drag(right, {right.x + 37, right.y});

    REQUIRE(f.selected().x == 100.5);
    const double rightEdge = f.selected().x + f.selected().w;
    REQUIRE(rightEdge == std::round(rightEdge));
}

TEST_CASE("the cursor follows the tool, the gesture and the handle under the pointer") {
    Fixture f = withRect();
    f.editor.pointerMove({100, 100});
    REQUIRE(f.editor.cursor() == "nwse-resize");
    f.editor.pointerMove({200, 100});
    REQUIRE(f.editor.cursor() == "nesw-resize");
    f.editor.pointerMove({150, 125});
    REQUIRE(f.editor.cursor() == "default");

    f.editor.setTool(Tool::Hand);
    REQUIRE(f.editor.cursor() == "grab");
    f.editor.pointerDown({10, 10}, PointerButton::Primary);
    REQUIRE(f.editor.cursor() == "grabbing");
    f.editor.pointerUp();

    f.editor.setTool(Tool::Ellipse);
    REQUIRE(f.editor.cursor() == "crosshair");
}

TEST_CASE("setProps from outside the canvas is one undoable op") {
    Fixture f = withRect();
    (void)f.session.takeOutbox();
    vc::NodePatch patch;
    patch.opacity = 0.5;
    patch.name = "Card";

    REQUIRE(f.editor.setProps(f.editor.selection().at(0), patch));
    REQUIRE(std::get<double>(f.selected().opacity) == 0.5);
    REQUIRE(f.session.takeOutbox().size() == 1);
    f.editor.undo();
    REQUIRE(f.selected().name == "Rectangle");
}

TEST_CASE("an empty patch is rejected instead of becoming an op") {
    Fixture f = withRect();
    (void)f.session.takeOutbox();

    REQUIRE_FALSE(f.editor.setProps(f.editor.selection().at(0), {}));
    REQUIRE(f.session.takeOutbox().empty());
}

TEST_CASE("a props preview shows live and commits as one op") {
    Fixture f = withRect();
    (void)f.session.takeOutbox();
    const vc::Id id = f.editor.selection().at(0);

    for (double l : {0.2, 0.4, 0.6}) {
        vc::NodePatch patch;
        patch.fill = vc::Oklch{l, 0.1, 30};
        REQUIRE(f.editor.previewProps(id, patch));
        REQUIRE(std::get<vc::Oklch>(f.selected().fill).l == l);
    }
    REQUIRE(f.session.takeOutbox().empty());

    REQUIRE(f.editor.commitPropsPreview());
    REQUIRE(f.session.takeOutbox().size() == 1);
    f.editor.undo();
    REQUIRE(std::get<vc::Oklch>(f.selected().fill).l == 0.89);
}

TEST_CASE("cancelling a props preview restores the node") {
    Fixture f = withRect();
    vc::NodePatch patch;
    patch.opacity = 0.2;
    f.editor.previewProps(f.editor.selection().at(0), patch);

    f.editor.cancelPropsPreview();
    REQUIRE(std::get<double>(f.selected().opacity) == 1.0);
    REQUIRE_FALSE(f.editor.commitPropsPreview());
}

TEST_CASE("starting a canvas gesture or undoing commits a pending props preview first") {
    Fixture f = withRect();
    vc::NodePatch patch;
    patch.opacity = 0.5;
    f.editor.previewProps(f.editor.selection().at(0), patch);

    f.drag({150, 125}, {160, 125});
    REQUIRE(std::get<double>(f.selected().opacity) == 0.5);
    REQUIRE(f.selected().x == 110);

    f.editor.undo();
    f.editor.undo();
    REQUIRE(std::get<double>(f.selected().opacity) == 1.0);
}

TEST_CASE("the structure revision ignores paint and geometry edits") {
    Fixture f = withRect();
    const vc::Id id = f.editor.selection().at(0);
    const auto structure = f.session.structureRevision();
    const auto any = f.editor.revision();

    vc::NodePatch paint;
    paint.fill = vc::Oklch{0.5, 0.1, 100};
    paint.x = 3;
    f.editor.setProps(id, paint);
    REQUIRE(f.session.structureRevision() == structure);
    REQUIRE(f.editor.revision() != any);

    vc::NodePatch rename;
    rename.name = "Card";
    f.editor.setProps(id, rename);
    REQUIRE(f.session.structureRevision() != structure);
}

TEST_CASE("locked nodes can't be picked on the canvas or resized") {
    Fixture f = withRect();
    const vc::Id id = f.editor.selection().at(0);
    vc::NodePatch lock;
    lock.locked = true;
    f.editor.setProps(id, lock);

    REQUIRE_FALSE(f.editor.handleBounds().has_value());
    f.editor.clearSelection();
    f.editor.pointerDown({150, 125}, PointerButton::Primary);
    f.editor.pointerUp();
    REQUIRE(f.editor.selection().empty());
}
