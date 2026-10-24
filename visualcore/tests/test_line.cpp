#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "vc/editor.hpp"
#include "vc/geometry.hpp"
#include "vc/draw_list.hpp"
#include "vc/json.hpp"
#include <limits>
using namespace vc;
using Catch::Matchers::WithinAbs;

namespace {
struct Lines {
    Session session{IdGenerator{119}}; Camera camera; Editor editor{session,camera};
    Node node(){Node n;n.id=session.newId();n.type=NodeType::Line;n.order="a0";n.x=20;n.y=30;n.w=100;n.h=80;return n;}
    void draw(Vec2 a,Vec2 b,Modifiers mods={}){editor.setTool(Tool::Line);editor.pointerDown(a,PointerButton::Primary,mods);editor.pointerMove(b,mods);editor.pointerUp();}
};
}

TEST_CASE("Native line gestures preserve signed diagonals and zero-axis geometry in one undo step") {
    for(const auto end:{Vec2{150,40},Vec2{50,140},Vec2{-50,140},Vec2{-50,-60}}){
        Lines f;f.draw({50,40},end);REQUIRE(f.session.document().size()==1);
        const auto* n=f.session.document().find(f.editor.selection().front());REQUIRE(n->type==NodeType::Line);
        REQUIRE(n->w==std::abs(end.x-50));REQUIRE(n->h==std::abs(end.y-40));
        REQUIRE(n->lineReverse==((end.x-50)*(end.y-40)<0));
        REQUIRE(f.editor.tool()==Tool::Select);REQUIRE(f.editor.undo());REQUIRE(f.session.document().size()==0);REQUIRE(f.editor.redo());REQUIRE(f.session.document().size()==1);
    }
    Lines click; click.editor.setTool(Tool::Line);click.editor.pointerDown({20,30},PointerButton::Primary);click.editor.pointerUp();
    REQUIRE(click.session.document().find(click.editor.selection().front())->h==0);
}

TEST_CASE("Line drawing supports angle constraints, centered gestures and cancellation") {
    Lines f;f.draw({100,100},{160,140},{true,true,false});
    const auto* n=f.session.document().find(f.editor.selection().front());
    REQUIRE(n->w==n->h);REQUIRE(n->x+n->w/2==100);REQUIRE(n->y+n->h/2==100);
    Lines cancel;cancel.editor.setTool(Tool::Line);cancel.editor.pointerDown({20,20},PointerButton::Primary);cancel.editor.pointerMove({100,100});cancel.editor.pointerCancel();
    REQUIRE(cancel.session.document().size()==0);REQUIRE_FALSE(cancel.editor.undo());
}

TEST_CASE("Line endpoint dragging crosses its anchor, keeps zero dimensions and restores with undo") {
    Lines f;auto n=f.node();REQUIRE(f.session.edit(CreateNode{n}));f.editor.select(n.id);
    f.editor.pointerDown({120,110},PointerButton::Primary);f.editor.pointerMove({0,150});f.editor.pointerUp();
    const auto* moved=f.session.document().find(n.id);REQUIRE(moved->x==0);REQUIRE(moved->y==30);REQUIRE(moved->w==20);REQUIRE(moved->h==120);REQUIRE(moved->lineReverse);
    REQUIRE(f.editor.undo());REQUIRE(f.session.document().find(n.id)->w==100);
    REQUIRE(f.editor.redo());REQUIRE(f.session.document().find(n.id)->lineReverse);
    f.editor.pointerDown({0,150},PointerButton::Primary);f.editor.pointerMove({20,150});f.editor.pointerUp();
    REQUIRE(f.session.document().find(n.id)->w==0);REQUIRE(f.session.document().find(n.id)->h==120);
}

TEST_CASE("Native line hit testing follows the segment, round cap, clipping and visibility") {
    Lines f;auto n=f.node();n.lineWidth=10;REQUIRE(f.session.edit(CreateNode{n}));
    REQUIRE(hitTest(f.session.document(),{70,70})==n.id);
    REQUIRE(hitTest(f.session.document(),{70,35}).isNil());
    REQUIRE(hitTest(f.session.document(),{16,30},false,kDefaultModeId,0)==n.id);
    NodePatch hidden;hidden.fillVisible=false;REQUIRE(f.editor.setProps(n.id,hidden));REQUIRE(hitTest(f.session.document(),{70,70}).isNil());
    REQUIRE(f.editor.undo());Node frame;frame.id=f.session.newId();frame.type=NodeType::Frame;frame.order="b0";frame.w=60;frame.h=60;frame.clipContent=true;
    REQUIRE(f.session.edit(CreateNode{frame}));REQUIRE(f.session.edit(MoveNode{n.id,frame.id,"a0"}));
    REQUIRE(hitTest(f.session.document(),{70,70}).isNil());
}

TEST_CASE("Line draw list emits a clipped capsule with token color, opacity and zero-length support") {
    Lines f;auto n=f.node();n.h=0;n.lineWidth=10;n.opacity=.5;
    const auto color=*f.editor.addToken(TokenType::Color);n.fill=TokenRef{color};
    REQUIRE(f.session.edit(CreateNode{n}));std::vector<DrawItem> items;
    REQUIRE(buildDrawList(f.session.document(),{0,0,200,200},kDefaultModeId,items)==1);REQUIRE(items.size()==1);
    REQUIRE(items[0].kind==DrawKind::Segment);REQUIRE(items[0].quad.h==10);REQUIRE(items[0].a.rect.x==20);REQUIRE(items[0].b.rect.x==120);REQUIRE(items[0].strokeWidth==10);
    REQUIRE_THAT(items[0].color.a,WithinAbs(.5,1e-6));
    NodePatch point;point.w=0;REQUIRE(f.editor.setProps(n.id,point));
    REQUIRE(buildDrawList(f.session.document(),{0,0,200,200},kDefaultModeId,items)==1);REQUIRE(items[0].quad.w==10);
    point.lineWidth=0;REQUIRE(f.editor.setProps(n.id,point));REQUIRE(buildDrawList(f.session.document(),{0,0,200,200},kDefaultModeId,items)==0);
}

TEST_CASE("Line width and direction inherit through components, persist in snapshot 12 and reset independently") {
    Lines f;Node main;main.id=f.session.newId();main.type=NodeType::Component;main.order="a0";main.w=200;main.h=200;REQUIRE(f.session.edit(CreateNode{main}));
    auto n=f.node();n.parent=main.id;REQUIRE(f.session.edit(CreateNode{n}));const auto instance=*f.editor.createInstance(main.id);
    const auto copy=f.session.document().viewChildren(instance).front();NodePatch local;local.lineWidth=8;REQUIRE(f.editor.setProps(copy,local));
    NodePatch source;source.lineWidth=4;source.lineReverse=true;REQUIRE(f.editor.setProps(n.id,source));
    REQUIRE(f.session.document().view(copy)->lineWidth==8);REQUIRE(f.session.document().view(copy)->lineReverse);
    const auto json=snapshotToJson(f.session.document());REQUIRE(json.find("\"version\":12")!=std::string::npos);
    const auto ops=snapshotFromJson(json);REQUIRE(ops.has_value());Document loaded;for(const auto& op:*ops)REQUIRE(loaded.apply(op));
    REQUIRE(loaded.view(copy)->lineWidth==8);REQUIRE(loaded.view(copy)->lineReverse);
    auto old=json;old.replace(old.find("\"version\":12"),12,"\"version\":11");REQUIRE_FALSE(snapshotFromJson(old).has_value());
    REQUIRE(f.editor.resetOverrides(copy));REQUIRE(f.session.document().view(copy)->lineWidth==4);REQUIRE(f.editor.undo());REQUIRE(f.session.document().view(copy)->lineWidth==8);
    ComponentDefinition definition;definition.axes.push_back({"weight",{"normal","heavy"},"normal"});
    NodePatch heavy;heavy.lineWidth=14;heavy.lineReverse=false;definition.rules={{{{"weight","heavy"}},n.id,heavy}};
    REQUIRE(f.editor.setComponentDefinition(main.id,definition));NodePatch choice;choice.componentValues=ComponentValues{{"weight",std::string("heavy")}};
    REQUIRE(f.editor.setProps(instance,choice));REQUIRE(f.session.document().view(copy)->lineWidth==8);REQUIRE_FALSE(f.session.document().view(copy)->lineReverse);
    REQUIRE(f.session.document().view(n.id)->lineReverse);REQUIRE(f.editor.resetOverrides(copy));REQUIRE(f.session.document().view(copy)->lineWidth==14);
    const auto variants=snapshotFromJson(snapshotToJson(f.session.document()));REQUIRE(variants.has_value());Document reloaded;for(const auto& op:*variants)REQUIRE(reloaded.apply(op));
    REQUIRE(reloaded.view(copy)->lineWidth==14);REQUIRE_FALSE(reloaded.view(copy)->lineReverse);
}

TEST_CASE("Lines refuse unsupported decorations, sizing and invalid width without changing stored geometry") {
    Lines f;auto n=f.node();REQUIRE(f.session.edit(CreateNode{n}));const auto before=snapshotToJson(f.session.document());
    for(int i=0;i<7;++i){NodePatch patch;
        if(i==0)patch.lineWidth=-1;if(i==1)patch.lineWidth=4097;if(i==2)patch.lineWidth=std::numeric_limits<double>::infinity();
        if(i==3)patch.cornerRadius=4.;if(i==4)patch.widthSizing=Sizing::Hug;if(i==5)patch.stroke=Stroke{2.};if(i==6)patch.clipContent=true;
        REQUIRE_FALSE(f.editor.setProps(n.id,patch));REQUIRE(snapshotToJson(f.session.document())==before);
    }
    Node rect;rect.id=f.session.newId();rect.order="b0";REQUIRE(f.session.edit(CreateNode{rect}));NodePatch patch;patch.lineWidth=2;REQUIRE_FALSE(f.editor.setProps(rect.id,patch));
}
