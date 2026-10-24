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
struct Paths {
    Session session{IdGenerator{125}}; Camera camera; Editor editor{session,camera};
    Node node(){Node n;n.id=session.newId();n.type=NodeType::Path;n.order="a0";n.x=20;n.y=30;n.w=100;n.h=80;n.path={{{0,0},{1,0},{1,1}},false};return n;}
};
}
TEST_CASE("Polyline normalization preserves authored vertex order, closed shape and degenerate axes") {
    for(const auto points: {std::vector<Vec2>{{90,50},{10,80},{40,20}},std::vector<Vec2>{{40,20},{40,80},{40,80}},std::vector<Vec2>{{20,20},{20,20},{20,20}}}) {
        const auto patch=pathGeometry(points,true); REQUIRE(patch.path);REQUIRE(validPath(*patch.path));
        Node n;applyPatch(n,patch);const auto roundtrip=pathPoints(n,{n.x,n.y,n.w,n.h});
        REQUIRE(roundtrip.size()==points.size());for(std::size_t i=0;i<points.size();++i){REQUIRE_THAT(roundtrip[i].x,WithinAbs(points[i].x,1e-8));REQUIRE_THAT(roundtrip[i].y,WithinAbs(points[i].y,1e-8));}
    }
    REQUIRE_FALSE(validPath(*pathGeometry({{0,0}},false).path));
    REQUIRE_FALSE(validPath(*pathGeometry({{0,0},{std::numeric_limits<double>::infinity(),0}},false).path));
}
TEST_CASE("Polyline picking follows each capsule, closed edge and clipping rather than its empty box") {
    Paths f;auto n=f.node();n.lineWidth=10;REQUIRE(f.session.edit(CreateNode{n}));
    REQUIRE(hitTest(f.session.document(),{70,30},false,kDefaultModeId,0)==n.id);
    REQUIRE(hitTest(f.session.document(),{120,70},false,kDefaultModeId,0)==n.id);
    REQUIRE(hitTest(f.session.document(),{16,30},false,kDefaultModeId,0)==n.id);
    REQUIRE(hitTest(f.session.document(),{70,70},false,kDefaultModeId,0).isNil());
    NodePatch close;close.path=n.path;close.path->closed=true;REQUIRE(f.editor.setProps(n.id,close));
    REQUIRE(hitTest(f.session.document(),{70,70},false,kDefaultModeId,0)==n.id);
    Node frame;frame.id=f.session.newId();frame.type=NodeType::Frame;frame.order="b0";frame.w=60;frame.h=60;frame.clipContent=true;
    REQUIRE(f.session.edit(CreateNode{frame}));REQUIRE(f.session.edit(MoveNode{n.id,frame.id,"a0"}));
    REQUIRE(hitTest(f.session.document(),{70,70}).isNil());
}
TEST_CASE("Polyline draw list represents all translucent joined segments in one union primitive") {
    Paths f;auto n=f.node();n.opacity=.5;n.lineWidth=10;n.path.closed=true;REQUIRE(f.session.edit(CreateNode{n}));
    std::vector<DrawItem> items;REQUIRE(buildDrawList(f.session.document(),{0,0,200,200},kDefaultModeId,items)==1);
    REQUIRE(items.size()==1);const auto& item=items.front();REQUIRE(item.kind==DrawKind::Path);REQUIRE(item.points.size()==3);REQUIRE(item.closed);REQUIRE(item.strokeWidth==10);
    REQUIRE(item.points[0].x==20);REQUIRE(item.points[2].y==110);REQUIRE(item.quad.x==15);REQUIRE_THAT(item.color.a,WithinAbs(.5,1e-6));
    NodePatch hide;hide.fillVisible=false;REQUIRE(f.editor.setProps(n.id,hide));REQUIRE(buildDrawList(f.session.document(),{0,0,200,200},kDefaultModeId,items)==0);
    REQUIRE(f.editor.undo());hide.lineWidth=0;hide.fillVisible=true;REQUIRE(f.editor.setProps(n.id,hide));REQUIRE(buildDrawList(f.session.document(),{0,0,200,200},kDefaultModeId,items)==0);
}
TEST_CASE("Polyline bounds and whole path overrides persist in snapshot 13 with independent inherited width") {
    Paths f;Node main;main.id=f.session.newId();main.type=NodeType::Component;main.order="a0";main.w=200;main.h=200;REQUIRE(f.session.edit(CreateNode{main}));
    auto n=f.node();n.parent=main.id;REQUIRE(f.session.edit(CreateNode{n}));const auto instance=*f.editor.createInstance(main.id);const auto copy=f.session.document().viewChildren(instance).front();
    NodePatch local;local.path=Path{{{0,0},{0,1},{1,1}},true};REQUIRE(f.editor.setProps(copy,local));
    NodePatch source;source.lineWidth=9;source.w=160;REQUIRE(f.editor.setProps(n.id,source));
    REQUIRE(f.session.document().view(copy)->path==*local.path);REQUIRE(f.session.document().view(copy)->lineWidth==9);REQUIRE(f.session.document().view(copy)->w==160);
    const auto json=snapshotToJson(f.session.document());REQUIRE(json.find("\"version\":13")!=std::string::npos);
    const auto ops=snapshotFromJson(json);REQUIRE(ops.has_value());Document loaded;for(const auto& op:*ops)REQUIRE(loaded.apply(op));REQUIRE(loaded.view(copy)->path==*local.path);
    auto old=json;old.replace(old.find("\"version\":13"),12,"\"version\":12");REQUIRE_FALSE(snapshotFromJson(old).has_value());
    REQUIRE(f.editor.resetOverrides(copy));REQUIRE(f.session.document().view(copy)->path==n.path);REQUIRE(f.editor.undo());REQUIRE(f.session.document().view(copy)->path==*local.path);
}
TEST_CASE("Polyline variant whole replacement leaves authored source untouched and yields to instance path") {
    Paths f;Node main;main.id=f.session.newId();main.type=NodeType::Component;main.order="a0";main.w=200;main.h=200;REQUIRE(f.session.edit(CreateNode{main}));
    auto n=f.node();n.parent=main.id;REQUIRE(f.session.edit(CreateNode{n}));ComponentDefinition def;def.axes.push_back({"shape",{"open","closed"},"open"});
    NodePatch closed;closed.path=Path{{{0,0},{1,0},{.5,1}},true};def.rules={{{{"shape","closed"}},n.id,closed}};REQUIRE(f.editor.setComponentDefinition(main.id,def));
    const auto instance=*f.editor.createInstance(main.id);const auto copy=f.session.document().viewChildren(instance).front();NodePatch choice;choice.componentValues=ComponentValues{{"shape",std::string("closed")}};REQUIRE(f.editor.setProps(instance,choice));
    REQUIRE(f.session.document().view(copy)->path==*closed.path);REQUIRE(f.session.document().view(n.id)->path==n.path);
    NodePatch local;local.path=Path{{{0,1},{1,0}},false};REQUIRE(f.editor.setProps(copy,local));REQUIRE(f.session.document().view(copy)->path==*local.path);
    const auto ops=snapshotFromJson(snapshotToJson(f.session.document()));REQUIRE(ops.has_value());Document loaded;for(const auto& op:*ops)REQUIRE(loaded.apply(op));REQUIRE(loaded.view(copy)->path==*local.path);
}
TEST_CASE("Polyline geometry rejects malformed, oversized and unsupported data atomically") {
    Paths f;auto n=f.node();REQUIRE(f.session.edit(CreateNode{n}));const auto before=snapshotToJson(f.session.document());
    for(int i=0;i<11;++i){NodePatch p;
        if(i==0)p.path=Path{{{0,0}},false};if(i==1)p.path=Path{{{0,0},{1,1}},true};if(i==2)p.path=Path{std::vector<Vec2>(65),false};
        if(i==3)p.path=Path{{{0,0},{1.1,1}},false};if(i==4)p.path=Path{{{0,0},{std::numeric_limits<double>::quiet_NaN(),0}},false};
        if(i==5)p.lineReverse=false;if(i==6)p.widthSizing=Sizing::Fill;if(i==7)p.cornerRadius=2.;if(i==8)p.stroke=Stroke{2.};if(i==9)p.clipContent=true;if(i==10)p.lineWidth=4097;
        REQUIRE_FALSE(f.editor.setProps(n.id,p));REQUIRE(snapshotToJson(f.session.document())==before);
    }
    Node rect;rect.id=f.session.newId();rect.order="b0";REQUIRE(f.session.edit(CreateNode{rect}));NodePatch p;p.path=n.path;REQUIRE_FALSE(f.editor.setProps(rect.id,p));
    n.id=f.session.newId();n.path.points.clear();REQUIRE_FALSE(f.session.edit(CreateNode{n}));
    REQUIRE_FALSE(patchFromJson(R"({"path":{"points":[{"x":0,"y":0},{"x":1,"y":1}],"closed":false,"curve":true}})"));
}

TEST_CASE("Cubic controls normalize together with vertices and retain their complete world geometry") {
    Path world{{{10,20},{110,20}},false,{{{10,20},{110,120}}},{{{10,120},{110,20}}},false};
    const auto patch=pathGeometry(world);REQUIRE(patch.path.has_value());REQUIRE(validPath(*patch.path));REQUIRE(*patch.w==100);REQUIRE(*patch.h==100);
    Node n;applyPatch(n,patch);REQUIRE(pathWorldGeometry(n,{n.x,n.y,n.w,n.h})==world);
    const auto points=flattenPath(n,{n.x,n.y,n.w,n.h});REQUIRE(points.size()>8);REQUIRE(points.size()<=4096);
    REQUIRE(points.front().x==10);REQUIRE(points.back().x==110);
    bool midpoint=false;for(const auto p:points) if(std::abs(p.x-60)<1e-8 && std::abs(p.y-95)<1e-8) midpoint=true;
    REQUIRE(midpoint);
    world.inHandles.clear();REQUIRE_FALSE(validPath(world));REQUIRE_FALSE(validPath(*pathGeometry(world).path));
}
TEST_CASE("Cubic picking follows the same flattened curve as its single draw primitive") {
    Paths f;auto n=f.node();const Path world{{{0,0},{100,0}},false,{{{0,0},{100,100}}},{{{0,100},{100,0}}},false};
    applyPatch(n,pathGeometry(world));REQUIRE(f.session.edit(CreateNode{n}));
    REQUIRE(hitTest(f.session.document(),{50,75},false,kDefaultModeId,0)==n.id);
    REQUIRE(hitTest(f.session.document(),{50,0},false,kDefaultModeId,0).isNil());
    std::vector<DrawItem> items;REQUIRE(buildDrawList(f.session.document(),{-10,-10,200,200},kDefaultModeId,items)==1);REQUIRE(items.size()==1);
    const auto flattened=flattenPath(n,{n.x,n.y,n.w,n.h});REQUIRE(items[0].points.size()==flattened.size());
    for(std::size_t i=0;i<flattened.size();++i){REQUIRE(items[0].points[i].x==flattened[i].x);REQUIRE(items[0].points[i].y==flattened[i].y);}
}
TEST_CASE("Even-odd path fill paints interiors and holes with zero stroke in one opacity layer") {
    Paths f;auto n=f.node();n.lineWidth=0;n.opacity=.4;
    Path world{{{0,0},{100,0},{100,100},{0,100},{0,0},{25,25},{75,25},{75,75},{25,75},{25,25},{0,0}},true};world.filled=true;
    applyPatch(n,pathGeometry(world));REQUIRE(f.session.edit(CreateNode{n}));
    REQUIRE(hitTest(f.session.document(),{10,50},false,kDefaultModeId,0)==n.id);
    REQUIRE(hitTest(f.session.document(),{50,50},false,kDefaultModeId,0).isNil());
    std::vector<DrawItem> items;REQUIRE(buildDrawList(f.session.document(),{-10,-10,200,200},kDefaultModeId,items)==1);REQUIRE(items.size()==1);REQUIRE(items[0].filled);REQUIRE(items[0].closed);REQUIRE(items[0].strokeWidth==0);REQUIRE_THAT(items[0].color.a,WithinAbs(.4,1e-6));
    NodePatch hidden;hidden.fillVisible=false;REQUIRE(f.editor.setProps(n.id,hidden));REQUIRE(hitTest(f.session.document(),{10,50}).isNil());
}
TEST_CASE("Cubic flattening preserves closed implicit edge and stays bounded for maximum curved paths") {
    Node n;n.type=NodeType::Path;n.w=1000000;n.h=1000000;n.path.closed=true;
    for(int i=0;i<64;++i){n.path.points.push_back({double(i%2),double((i/2)%2)});n.path.inHandles.push_back({double((i+1)%2),1.});n.path.outHandles.push_back({double(i%2),0.});}
    REQUIRE(validPath(n.path));const auto points=flattenPath(n,{0,0,n.w,n.h},1e-12);REQUIRE(points.size()<=4096);REQUIRE(points.size()>64);
    const auto again=flattenPath(n,{0,0,n.w,n.h},1e-12);REQUIRE(again.size()==points.size());
    for(std::size_t i=0;i<points.size();++i){REQUIRE(again[i].x==points[i].x);REQUIRE(again[i].y==points[i].y);}
}
TEST_CASE("Cubic and filled paths persist only in snapshot14 while straight paths retain13") {
    Paths f;auto n=f.node();REQUIRE(f.session.edit(CreateNode{n}));REQUIRE(snapshotToJson(f.session.document()).find("\"version\":13")!=std::string::npos);
    NodePatch p;p.path=n.path;p.path->filled=true;p.path->closed=true;p.path->inHandles=n.path.points;p.path->outHandles=n.path.points;REQUIRE(f.editor.setProps(n.id,p));
    const auto json=snapshotToJson(f.session.document());REQUIRE(json.find("\"version\":14")!=std::string::npos);const auto ops=snapshotFromJson(json);REQUIRE(ops.has_value());Document loaded;for(const auto& op:*ops)REQUIRE(loaded.apply(op));REQUIRE(loaded.find(n.id)->path==*p.path);
    auto old=json;old.replace(old.find("\"version\":14"),12,"\"version\":13");REQUIRE_FALSE(snapshotFromJson(old).has_value());
    REQUIRE(f.editor.undo());REQUIRE(f.session.document().find(n.id)->path==n.path);
    REQUIRE_FALSE(patchFromJson(R"({"path":{"points":[{"x":0,"y":0},{"x":1,"y":1}],"closed":false,"filled":true}})").has_value());
    REQUIRE_FALSE(patchFromJson(R"({"path":{"points":[{"x":0,"y":0},{"x":1,"y":1}],"closed":false,"inHandles":[{"x":0,"y":0}],"outHandles":[{"x":0,"y":0}]}})").has_value());
}
