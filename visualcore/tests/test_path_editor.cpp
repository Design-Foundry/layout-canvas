#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "vc/editor.hpp"
#include "vc/geometry.hpp"

using namespace vc;
using Catch::Matchers::WithinAbs;

namespace {
struct Paths {
    Session session{IdGenerator{127}};
    Camera camera;
    Editor editor{session,camera};
    Paths() { editor.setTool(Tool::Pen); }
    void click(Vec2 p, Modifiers mods={}) { editor.pointerDown(p,PointerButton::Primary,mods); editor.pointerUp(); }
    const Node* selected() const { return session.document().find(editor.selection().front()); }
};
}

TEST_CASE("Pen previews the cursor but finishes only authored vertices in one undo step") {
    Paths f;
    REQUIRE(toolFromName("pen")==Tool::Pen);
    f.click({20,30});
    REQUIRE(f.editor.drawingPath());
    REQUIRE(f.session.hasPreview());
    REQUIRE(f.session.takeOutbox().empty());
    f.editor.pointerMove({70,90});
    REQUIRE(f.session.takeOutbox().empty());
    f.click({120,30});
    f.click({120,130});
    f.editor.pointerMove({300,300});
    REQUIRE(f.editor.finishPath());
    REQUIRE_FALSE(f.editor.drawingPath());
    REQUIRE(f.editor.tool()==Tool::Select);
    const auto id=f.selected()->id;
    REQUIRE(f.selected()->type==NodeType::Path);
    REQUIRE(f.selected()->path.points.size()==3);
    REQUIRE(f.selected()->x==20);
    REQUIRE(f.selected()->w==100);
    REQUIRE(f.selected()->h==100);
    REQUIRE(f.session.takeOutbox().size()==1);
    REQUIRE(f.editor.undo());
    REQUIRE(f.session.document().size()==0);
    REQUIRE_FALSE(f.editor.undo());
    REQUIRE(f.editor.redo());
    REQUIRE(f.session.document().find(id)->path.points.size()==3);
}

TEST_CASE("Pen cancellation, tool switching and singleton finishing never create an edit") {
    for(int mode=0;mode<4;++mode) {
        Paths f; f.click({20,30}); f.editor.pointerMove({120,130});
        if(mode==0) f.editor.pointerCancel();
        if(mode==1) f.editor.cancelPath();
        if(mode==2) f.editor.setTool(Tool::Rect);
        if(mode==3) REQUIRE_FALSE(f.editor.finishPath());
        REQUIRE_FALSE(f.editor.drawingPath());
        REQUIRE_FALSE(f.session.hasPreview());
        REQUIRE(f.session.document().size()==0);
        REQUIRE(f.session.takeOutbox().empty());
        REQUIRE_FALSE(f.editor.undo());
    }
}

TEST_CASE("Pen removes pending vertices, ignores double-click duplicates and closes at the first vertex") {
    Paths f; f.click({20,30}); f.click({120,30}); f.click({120,130});
    REQUIRE(f.editor.popPathPoint());
    f.click({150,150});
    f.click({150,150}); // second pointer-down of a browser double click
    f.editor.doubleClick({150,150});
    REQUIRE(f.selected()->path.points.size()==3);
    REQUIRE_FALSE(f.selected()->path.closed);
    Paths closed; closed.click({20,30}); closed.click({120,30}); closed.click({120,130}); closed.click({20,30});
    REQUIRE_FALSE(closed.editor.drawingPath());
    REQUIRE(closed.selected()->path.closed);
    REQUIRE(closed.selected()->path.points.size()==3);
    Paths empty; empty.click({20,30}); REQUIRE(empty.editor.popPathPoint());
    REQUIRE_FALSE(empty.editor.popPathPoint());
    REQUIRE(empty.session.document().size()==0);
}

TEST_CASE("Pen limits authored points to 64 and shift constrains the next segment") {
    Paths f; f.click({20,30}); f.click({120,90},{true,false,false});
    REQUIRE(f.editor.finishPath());
    REQUIRE_THAT(f.selected()->w,WithinAbs(f.selected()->h,1e-9));
    Paths capped;
    for(int i=0;i<80;++i) capped.click({static_cast<double>(i*10),40});
    capped.editor.pointerMove({1000,1000});
    REQUIRE(capped.editor.finishPath());
    REQUIRE(capped.selected()->path.points.size()==64);
    REQUIRE(capped.selected()->w==630);
    REQUIRE(capped.selected()->h==0);
}

TEST_CASE("Path vertex dragging uses original coordinates across repeated updates and crossing bounds") {
    Paths f; f.click({20,30}); f.click({120,30}); f.click({120,130}); REQUIRE(f.editor.finishPath());
    const auto id=f.selected()->id;
    REQUIRE(f.editor.handleAt({20,30}).point==0);
    f.editor.pointerDown({20,30},PointerButton::Primary);
    f.editor.pointerMove({150,160});
    f.editor.pointerMove({180,190});
    f.editor.pointerUp();
    auto points=pathPoints(*f.selected(),*worldBounds(f.session.document(),id));
    REQUIRE(points[0].x==180); REQUIRE(points[0].y==190);
    REQUIRE(points[1].x==120); REQUIRE(points[1].y==30);
    REQUIRE(points[2].x==120); REQUIRE(points[2].y==130);
    REQUIRE(f.editor.undo());
    REQUIRE(f.session.document().find(id)->x==20);
    REQUIRE(f.editor.redo());
    f.editor.select(id);
    points=pathPoints(*f.selected(),*worldBounds(f.session.document(),id));
    REQUIRE(points[0].x==180);
    f.editor.pointerDown({180,190},PointerButton::Primary);
    f.editor.pointerMove({0,0});
    f.editor.pointerCancel();
    REQUIRE(f.selected()->x==120);
}

TEST_CASE("Inspector edits cancel a pending path instead of accidentally committing it") {
    Paths f; Node n; n.id=f.session.newId(); n.order="a0";
    REQUIRE(f.session.edit(CreateNode{n})); static_cast<void>(f.session.takeOutbox());
    f.click({300,300}); f.click({400,400});
    NodePatch patch; patch.name="Updated";
    REQUIRE(f.editor.setProps(n.id,patch));
    REQUIRE_FALSE(f.editor.drawingPath());
    REQUIRE(f.session.document().size()==1);
    REQUIRE(f.session.takeOutbox().size()==1);
    REQUIRE(f.editor.undo());
    REQUIRE(f.session.document().size()==1);
}

TEST_CASE("Pen stores frame-local points and source vertex handles honor auto layout and copies") {
    Paths f;
    Node frame; frame.id=f.session.newId(); frame.type=NodeType::Component;
    frame.order="a0"; frame.x=100; frame.y=100; frame.w=300; frame.h=300;
    REQUIRE(f.session.edit(CreateNode{frame}));
    f.click({120,130}); f.click({220,130}); f.click({220,230});
    REQUIRE(f.editor.finishPath());
    const auto pathId=f.selected()->id;
    REQUIRE(f.selected()->parent==frame.id);
    REQUIRE(f.selected()->x==20);
    REQUIRE(f.selected()->y==30);
    REQUIRE(f.editor.handleAt({120,130}).point==0);
    const auto instance=f.editor.createInstance(frame.id);
    REQUIRE(instance.has_value());
    const auto copy=f.session.document().viewChildren(*instance).front();
    f.editor.select(copy);
    REQUIRE_FALSE(f.editor.handleBounds().has_value());
    f.editor.select(pathId);
    NodePatch flow; flow.absolute=false;
    REQUIRE(f.editor.setProps(pathId,flow));
    NodePatch layout; Layout row; row.type=LayoutType::Row; layout.layout=row;
    REQUIRE(f.editor.setProps(frame.id,layout));
    REQUIRE_FALSE(f.editor.handleBounds().has_value());
}

TEST_CASE("Sidebar selection and live inspector previews discard unfinished paths without outbox edits") {
    for(int action=0;action<5;++action) {
        Paths f; Node source; source.id=f.session.newId(); source.order="a0";
        REQUIRE(f.session.edit(CreateNode{source}));
        static_cast<void>(f.session.takeOutbox());
        f.click({300,300}); f.click({400,400}); f.editor.pointerMove({500,300});
        // Leaving the canvas invokes pointerUp; a sidebar click must never commit the pen.
        f.editor.pointerUp();
        REQUIRE(f.editor.drawingPath());
        REQUIRE(f.session.takeOutbox().empty());
        if(action==0) f.editor.select(source.id);
        if(action==1) f.editor.toggleSelection(source.id);
        if(action==2) f.editor.setSelection({source.id});
        if(action==3) f.editor.clearSelection();
        if(action==4) {
            NodePatch preview; preview.opacity=.5;
            REQUIRE(f.editor.previewProps(source.id,preview));
            f.editor.cancelPropsPreview();
        }
        REQUIRE_FALSE(f.editor.drawingPath());
        REQUIRE_FALSE(f.session.hasPreview());
        REQUIRE(f.session.document().size()==1);
        REQUIRE(f.session.takeOutbox().empty());
        REQUIRE(f.editor.undo());
        REQUIRE(f.session.document().size()==0);
    }
}

TEST_CASE("Pen dragging authors symmetric cubic controls and later clicks preserve them") {
    Paths f;
    f.editor.pointerDown({20,30},PointerButton::Primary);
    f.editor.pointerMove({20,80});
    f.editor.pointerUp();
    f.click({120,130});
    f.editor.pointerMove({300,300});
    REQUIRE(f.editor.finishPath());
    const auto id=f.selected()->id;
    auto path=pathWorldGeometry(*f.selected(),*worldBounds(f.session.document(),id));
    REQUIRE(path.points.size()==2);
    REQUIRE(path.inHandles.size()==2);
    REQUIRE_THAT(path.points[0].x,WithinAbs(20,1e-9));
    REQUIRE_THAT(path.points[0].y,WithinAbs(30,1e-9));
    REQUIRE_THAT(path.outHandles[0].y,WithinAbs(80,1e-9));
    REQUIRE_THAT(path.inHandles[0].y,WithinAbs(-20,1e-9));
    REQUIRE_THAT(path.inHandles[1].x,WithinAbs(120,1e-9));
    REQUIRE_FALSE(path.filled);
    REQUIRE(f.session.takeOutbox().size()==1);
    REQUIRE(f.editor.undo());REQUIRE(f.session.document().size()==0);
    REQUIRE(f.editor.redo());REQUIRE(f.session.document().find(id)->path.inHandles.size()==2);
}

TEST_CASE("Curve anchor and control drags keep original geometry and support independent Alt handles") {
    Paths f;
    f.editor.pointerDown({20,30},PointerButton::Primary);f.editor.pointerMove({20,80});f.editor.pointerUp();
    f.click({120,130});REQUIRE(f.editor.finishPath());
    const auto id=f.selected()->id;
    REQUIRE(f.editor.handleAt({20,80}).control==2);
    f.editor.pointerDown({20,80},PointerButton::Primary);
    f.editor.pointerMove({50,90});f.editor.pointerMove({70,100});f.editor.pointerUp();
    auto path=pathWorldGeometry(*f.selected(),*worldBounds(f.session.document(),id));
    REQUIRE_THAT(path.outHandles[0].x,WithinAbs(70,1e-9));
    REQUIRE_THAT(path.outHandles[0].y,WithinAbs(100,1e-9));
    REQUIRE_THAT(path.inHandles[0].x,WithinAbs(-30,1e-9));
    REQUIRE_THAT(path.inHandles[0].y,WithinAbs(-40,1e-9));
    f.editor.pointerDown({70,100},PointerButton::Primary,{false,true,false});
    f.editor.pointerMove({90,110},{false,true,false});f.editor.pointerUp();
    path=pathWorldGeometry(*f.selected(),*worldBounds(f.session.document(),id));
    REQUIRE_THAT(path.inHandles[0].x,WithinAbs(-30,1e-9));
    REQUIRE_THAT(path.inHandles[0].y,WithinAbs(-40,1e-9));
    REQUIRE_THAT(path.outHandles[0].x,WithinAbs(90,1e-9));
    f.editor.pointerDown({20,30},PointerButton::Primary);
    f.editor.pointerMove({200,200});f.editor.pointerMove({220,230});f.editor.pointerUp();
    path=pathWorldGeometry(*f.selected(),*worldBounds(f.session.document(),id));
    REQUIRE_THAT(path.points[0].x,WithinAbs(220,1e-9));
    REQUIRE_THAT(path.points[0].y,WithinAbs(230,1e-9));
    REQUIRE_THAT(path.inHandles[0].x,WithinAbs(170,1e-9));
    REQUIRE_THAT(path.inHandles[0].y,WithinAbs(160,1e-9));
    REQUIRE_THAT(path.outHandles[0].x,WithinAbs(290,1e-9));
    REQUIRE_THAT(path.outHandles[0].y,WithinAbs(310,1e-9));
    REQUIRE(f.editor.undo());REQUIRE(f.editor.redo());
    f.editor.select(id);f.editor.pointerDown({290,310},PointerButton::Primary);
    f.editor.pointerMove({500,500});f.editor.pointerCancel();
    path=pathWorldGeometry(*f.selected(),*worldBounds(f.session.document(),id));
    REQUIRE_THAT(path.outHandles[0].x,WithinAbs(290,1e-9));
}

TEST_CASE("Pencil decimates deterministically to 64 samples and retains both endpoints in one undo step") {
    Paths f,g;
    for(auto* fixture:{&f,&g}) {
        fixture->editor.setTool(Tool::Pencil);
        fixture->editor.pointerDown({20,30},PointerButton::Primary);
        for(int i=1;i<=150;++i) fixture->editor.pointerMove({20.+i*2,30.+(i%7)*5});
        REQUIRE(fixture->session.takeOutbox().empty());
        fixture->editor.pointerUp();
        REQUIRE(fixture->editor.tool()==Tool::Select);
        REQUIRE(fixture->selected()->path.points.size()==64);
        const auto path=pathWorldGeometry(*fixture->selected(),*worldBounds(fixture->session.document(),fixture->selected()->id));
        REQUIRE_THAT(path.points.front().x,WithinAbs(20,1e-9));
        REQUIRE_THAT(path.points.front().y,WithinAbs(30,1e-9));
        REQUIRE_THAT(path.points.back().x,WithinAbs(320,1e-9));
        REQUIRE_THAT(path.points.back().y,WithinAbs(45,1e-9));
        REQUIRE_FALSE(path.closed);REQUIRE_FALSE(path.filled);REQUIRE(path.inHandles.empty());
        REQUIRE(fixture->session.takeOutbox().size()==1);
    }
    REQUIRE(f.selected()->path==g.selected()->path);
    REQUIRE(f.editor.undo());REQUIRE(f.session.document().size()==0);
    REQUIRE_FALSE(f.editor.undo());REQUIRE(f.editor.redo());REQUIRE(f.session.document().size()==1);
}

TEST_CASE("Pencil cancels safely and a click creates no singleton drawing") {
    for(int action=0;action<3;++action) {
        Paths f;f.editor.setTool(Tool::Pencil);f.editor.pointerDown({20,30},PointerButton::Primary);
        if(action!=0) f.editor.pointerMove({120,130});
        if(action==0) f.editor.pointerUp();
        if(action==1) f.editor.pointerCancel();
        if(action==2) f.editor.setTool(Tool::Rect);
        REQUIRE(f.session.document().size()==0);REQUIRE(f.session.takeOutbox().empty());REQUIRE_FALSE(f.editor.undo());
    }
}

TEST_CASE("Polygon and star gestures create editable filled paths with centered and square bounds") {
    for(const auto tool:{Tool::Polygon,Tool::Star}) {
        Paths f;f.editor.setTool(tool);f.editor.pointerDown({100,100},PointerButton::Primary,{true,true,false});
        f.editor.pointerMove({160,140},{true,true,false});f.editor.pointerUp();
        REQUIRE(f.selected()->type==NodeType::Path);
        REQUIRE(f.selected()->path.points.size()==(tool==Tool::Star?10:5));
        REQUIRE(f.selected()->path.closed);REQUIRE(f.selected()->path.filled);
        REQUIRE(f.selected()->path.inHandles.empty());
        REQUIRE(f.selected()->w==120);REQUIRE(f.selected()->h==120);
        REQUIRE(f.selected()->x==40);REQUIRE(f.selected()->y==40);
        REQUIRE(f.editor.undo());REQUIRE(f.session.document().size()==0);REQUIRE(f.editor.redo());
        Paths click;click.editor.setTool(tool);click.click({20,30});
        REQUIRE(click.selected()->w==Editor::kDefaultShapeSize);REQUIRE(click.selected()->path.filled);
    }
    REQUIRE(toolFromName("pencil")==Tool::Pencil);REQUIRE(toolFromName("polygon")==Tool::Polygon);REQUIRE(toolFromName("star")==Tool::Star);
}
