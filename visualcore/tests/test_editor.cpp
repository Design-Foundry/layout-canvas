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
