#include <catch2/catch_test_macros.hpp>
#include <array>
#include <algorithm>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "vc/editor.hpp"
#include "vc/draw_list.hpp"
#include "vc/geometry.hpp"
#include "vc/json.hpp"

using namespace vc;
using Catch::Matchers::WithinAbs;

namespace {
struct Prototype {
    Session session{IdGenerator{199}};
    Camera camera;
    Editor editor{session,camera};
    Id a,b,button;
    Prototype() {
        a=add(NodeType::Frame,{},100,200,200,100);
        b=add(NodeType::Frame,{},700,200,200,100);
        button=add(NodeType::Rect,a,10,10,60,40);
        navigate(button,b);
    }
    Id add(NodeType type,Id parent={},double x=0,double y=0,double w=50,double h=40) {
        Node node;node.id=session.newId();node.type=type;node.parent=parent;node.order="a0";
        node.x=x;node.y=y;node.w=w;node.h=h;node.name="Prototype";
        REQUIRE(session.applyRemote(CreateNode{node}));return node.id;
    }
    void patch(Id id,const NodePatch& value) {REQUIRE(session.applyRemote(SetProps{id,value}));}
    void navigate(Id source,Id target) {NodePatch p;p.interaction=Interaction{PrototypeAction::Navigate,target};patch(source,p);}
    void click(Vec2 world,PointerButton button=PointerButton::Primary) {
        const auto screen=camera.toScreen(world);editor.pointerDown(screen,button);editor.pointerUp();
    }
    Vec2 center(Id id) const {const auto box=*worldBounds(session.document(),id);return {box.x+box.w/2,box.y+box.h/2};}
};
}

TEST_CASE("Prototype entry validates screens and restores camera and selection without document history") {
    Prototype f;f.camera.pan({-43,27});f.camera.zoomAt({20,50},1.75);f.editor.select(f.button);
    const auto old=f.camera;
    const auto selection=f.editor.selection();
    const auto before=snapshotToJson(f.session.document());
    REQUIRE_FALSE(f.editor.startPrototype(f.button,{448,248}));
    REQUIRE_FALSE(f.editor.startPrototype(f.a,{0,248}));
    REQUIRE(f.editor.startPrototype(f.a,{448,248}));
    REQUIRE(f.editor.presenting());REQUIRE(f.editor.prototypeScreen()==f.a);
    REQUIRE_FALSE(f.editor.canPrototypeBack());
    REQUIRE(f.editor.selection()==selection);
    REQUIRE_THAT(f.camera.toScreen({100,200}).x,WithinAbs(24,1e-9));
    REQUIRE_THAT(f.camera.toScreen({100,200}).y,WithinAbs(24,1e-9));
    f.editor.stopPrototype();
    REQUIRE_FALSE(f.editor.presenting());REQUIRE(f.editor.prototypeScreen().isNil());
    REQUIRE(f.camera.zoom()==old.zoom());REQUIRE(f.camera.position().x==old.position().x);REQUIRE(f.camera.position().y==old.position().y);
    REQUIRE(f.editor.selection()==selection);REQUIRE(snapshotToJson(f.session.document())==before);
    REQUIRE(f.session.takeOutbox().empty());REQUIRE_FALSE(f.session.canUndo());
}

TEST_CASE("Prototype scoped picking ignores overlapping screens and bubbles deepest children to hotspots") {
    Prototype f;NodePatch overlap;overlap.x=100;f.patch(f.b,overlap);
    const auto hotspot=f.add(NodeType::Frame,f.a,90,10,60,40);f.navigate(hotspot,f.b);
    const auto child=f.add(NodeType::Rect,hotspot,5,5,15,15);
    REQUIRE(hitTest(f.session.document(),f.center(child),false,kDefaultModeId,4,f.a)==child);
    REQUIRE(f.editor.startPrototype(f.a,{448,248}));
    f.click(f.center(child));REQUIRE(f.editor.prototypeScreen()==f.b);REQUIRE(f.editor.canPrototypeBack());
    REQUIRE(f.editor.prototypeBack());REQUIRE(f.editor.prototypeScreen()==f.a);
    REQUIRE_FALSE(f.editor.prototypeBack());
    f.click(f.center(child));REQUIRE(f.editor.restartPrototype());
    REQUIRE(f.editor.prototypeScreen()==f.a);REQUIRE_FALSE(f.editor.canPrototypeBack());
    REQUIRE(f.session.takeOutbox().empty());
}

TEST_CASE("Prototype clicks honor cancellation, drag slop, pointer buttons and design-locked hotspots") {
    Prototype f;NodePatch locked;locked.locked=true;f.patch(f.button,locked);
    REQUIRE(hitTest(f.session.document(),f.center(f.button),false,kDefaultModeId,4,f.a)!=f.button);
    REQUIRE(f.editor.startPrototype(f.a,{448,248}));
    auto point=f.camera.toScreen(f.center(f.button));
    f.editor.pointerDown(point,PointerButton::Primary);f.editor.pointerCancel();f.editor.pointerUp();
    REQUIRE(f.editor.prototypeScreen()==f.a);
    f.editor.pointerDown(point,PointerButton::Primary);f.editor.pointerMove({point.x+10,point.y});f.editor.pointerMove(point);f.editor.pointerUp();
    REQUIRE(f.editor.prototypeScreen()==f.a);
    f.click(f.center(f.button),PointerButton::Secondary);f.click(f.center(f.button),PointerButton::Middle);
    REQUIRE(f.editor.prototypeScreen()==f.a);
    f.editor.pointerMove(point);REQUIRE(f.editor.cursor()=="pointer");
    f.click(f.center(f.button));REQUIRE(f.editor.prototypeScreen()==f.b);
}

TEST_CASE("Prototype picking clips nested content and the active screen even when clipContent is off") {
    Prototype f;NodePatch clear;clear.interaction=Interaction{};f.patch(f.button,clear);
    const auto nested=f.add(NodeType::Frame,f.a,10,10,30,30);
    REQUIRE_FALSE(f.editor.startPrototype(nested,{448,248}));
    const auto clipped=f.add(NodeType::Rect,nested,20,0,40,20);f.navigate(clipped,f.b);
    NodePatch clipping;clipping.clipContent=true;f.patch(nested,clipping);
    const auto outside=f.add(NodeType::Rect,f.a,210,20,30,30);f.navigate(outside,f.b);
    REQUIRE(f.editor.startPrototype(f.a,{448,248}));
    f.click({155,215});REQUIRE(f.editor.prototypeScreen()==f.a);
    f.click(f.center(outside));REQUIRE(f.editor.prototypeScreen()==f.a);
    f.click({135,215});REQUIRE(f.editor.prototypeScreen()==f.b);
    REQUIRE(f.editor.prototypeBack());
    NodePatch hidden;hidden.visible=false;f.patch(nested,hidden);
    f.click({135,215});REQUIRE(f.editor.prototypeScreen()==f.a);
}

TEST_CASE("Prototype activation requires pressing and releasing the same hotspot inside the screen") {
    Prototype f;const auto edge=f.add(NodeType::Rect,f.a,0,0,10,10);f.navigate(edge,f.b);
    const auto adjacent=f.add(NodeType::Rect,f.a,10,0,10,10);f.navigate(adjacent,f.b);
    REQUIRE(f.editor.startPrototype(f.a,{448,248}));
    f.editor.pointerDown(f.camera.toScreen({99.8,205}),PointerButton::Primary);
    f.editor.pointerMove(f.camera.toScreen({100.2,205}));f.editor.pointerUp();
    REQUIRE(f.editor.prototypeScreen()==f.a);
    f.editor.pointerDown(f.camera.toScreen({109.6,205}),PointerButton::Primary);
    f.editor.pointerMove(f.camera.toScreen({110.4,205}));f.editor.pointerUp();
    REQUIRE(f.editor.prototypeScreen()==f.a);
    f.click({105,205});REQUIRE(f.editor.prototypeScreen()==f.b);
}

TEST_CASE("Prototype instance hotspots inherit component actions and allow local back overrides") {
    Prototype f;
    const auto main=f.add(NodeType::Component,{},1000,1000,60,40);f.navigate(main,f.b);
    REQUIRE_FALSE(f.editor.startPrototype(main,{448,248}));
    Node instance;instance.id=f.session.newId();instance.type=NodeType::Instance;instance.parent=f.a;instance.component=main;instance.order="a1";
    instance.x=90;instance.y=10;instance.w=60;instance.h=40;
    REQUIRE(f.session.applyRemote(CreateNode{instance}));
    REQUIRE(f.editor.startPrototype(f.a,{448,248}));f.click(f.center(instance.id));
    REQUIRE(f.editor.prototypeScreen()==f.b);
    Node back;back.id=f.session.newId();back.type=NodeType::Instance;back.parent=f.b;back.component=main;back.order="a1";back.w=60;back.h=40;
    REQUIRE(f.session.applyRemote(CreateNode{back}));
    NodePatch action;action.interaction=Interaction{PrototypeAction::Back,{}};
    REQUIRE(f.session.applyRemote(SetOverride{back.id,main,action}));
    f.click(f.center(back.id));REQUIRE(f.editor.prototypeScreen()==f.a);
    REQUIRE_FALSE(f.editor.canPrototypeBack());REQUIRE(f.session.takeOutbox().empty());
}

TEST_CASE("Prototype history is bounded and self navigation never consumes a back entry") {
    Prototype f;f.navigate(f.a,f.b);f.navigate(f.b,f.a);
    REQUIRE(f.editor.startPrototype(f.a,{448,248}));
    for(int i=0;i<260;++i) f.click({f.editor.prototypeScreen()==f.a?280.:880.,280});
    int count=0;while(f.editor.prototypeBack()) ++count;
    REQUIRE(count==128);
    REQUIRE(f.editor.restartPrototype());f.navigate(f.a,f.a);
    f.click({280,280});REQUIRE(f.editor.prototypeScreen()==f.a);REQUIRE_FALSE(f.editor.canPrototypeBack());
}

TEST_CASE("Prototype refuses every editor mutation and camera gesture while allowing remote updates") {
    Prototype f;f.editor.select(f.button);const auto before=snapshotToJson(f.session.document());
    REQUIRE(f.editor.startPrototype(f.a,{448,248}));const auto camera=f.camera;
    NodePatch patch;patch.name="Refused";
    REQUIRE_FALSE(f.editor.edit(SetProps{f.button,patch}));REQUIRE_FALSE(f.editor.setProps(f.button,patch));
    REQUIRE_FALSE(f.editor.previewProps(f.button,patch));REQUIRE_FALSE(f.editor.commitPropsPreview());
    REQUIRE_FALSE(f.editor.addToken(TokenType::Color).has_value());REQUIRE_FALSE(f.editor.addMode().has_value());
    REQUIRE_FALSE(f.editor.deleteMode(kDefaultModeId));REQUIRE_FALSE(f.editor.setComponentDefinition(f.a,{}));
    REQUIRE_FALSE(f.editor.createComponent().has_value());REQUIRE_FALSE(f.editor.createInstance(f.a).has_value());
    REQUIRE_FALSE(f.editor.detachInstance(f.button).has_value());REQUIRE_FALSE(f.editor.resetOverrides(f.button));
    REQUIRE_FALSE(f.editor.nudge({10,10}));REQUIRE_FALSE(f.editor.deleteSelection());REQUIRE_FALSE(f.editor.undo());REQUIRE_FALSE(f.editor.redo());
    REQUIRE_FALSE(f.editor.finishPath());REQUIRE_FALSE(f.editor.popPathPoint());
    REQUIRE_FALSE(f.editor.importImage(Raster{},"Refused").has_value());
    f.editor.select(f.b);f.editor.toggleSelection(f.b);f.editor.setSelection({f.b});f.editor.clearSelection();
    REQUIRE(f.editor.selection()==std::vector<Id>{f.button});
    f.editor.setTool(Tool::Rect);REQUIRE(f.editor.tool()==Tool::Select);
    f.editor.setPixelSnap(false);REQUIRE(f.editor.pixelSnap());
    f.editor.reveal(f.b,{300,300});f.editor.wheel({10,10},100);f.editor.doubleClick({10,10});
    REQUIRE(f.camera.zoom()==camera.zoom());REQUIRE(f.camera.position().x==camera.position().x);
    REQUIRE(snapshotToJson(f.session.document())==before);REQUIRE(f.session.takeOutbox().empty());REQUIRE_FALSE(f.session.canUndo());
    f.patch(f.button,patch);REQUIRE(f.session.document().find(f.button)->name=="Refused");
}

TEST_CASE("Prototype refits on resize and exits safely when active or start screens become invalid remotely") {
    for(int invalid=0;invalid<5;++invalid) {
        Prototype f;f.camera.pan({-45,-12});const auto old=f.camera;
        REQUIRE(f.editor.startPrototype(f.a,{448,248}));f.click(f.center(f.button));
        f.editor.resizePrototype({848,448});
        REQUIRE_THAT(f.camera.toScreen({800,250}).x,WithinAbs(424,1e-9));
        REQUIRE_THAT(f.camera.toScreen({800,250}).y,WithinAbs(224,1e-9));
        if(invalid==0) {NodePatch p;p.visible=false;f.patch(f.b,p);}
        if(invalid==1) {NodePatch p;p.w=0;f.patch(f.b,p);}
        if(invalid==2) {NodePatch p;p.visible=false;f.patch(f.a,p);}
        if(invalid==3 || invalid==4) {NodePatch p;p.interaction=Interaction{};f.patch(f.button,p);}
        if(invalid==3) REQUIRE(f.session.applyRemote(DeleteNode{f.b}));
        if(invalid==4) {NodePatch p;p.type=NodeType::Component;f.patch(f.b,p);}
        f.editor.resizePrototype({848,448});REQUIRE_FALSE(f.editor.presenting());
        REQUIRE(f.camera.zoom()==old.zoom());REQUIRE(f.camera.position().x==old.position().x);REQUIRE(f.camera.position().y==old.position().y);
    }
}

TEST_CASE("Scoped presentation draw lists show only one root and clip overflowing content to its bounds") {
    Prototype f;const auto outside=f.add(NodeType::Rect,f.a,180,80,80,80);
    static_cast<void>(outside);
    std::vector<DrawItem> items;
    const auto count=buildDrawList(f.session.document(),{-1000,-1000,4000,4000},kDefaultModeId,items,f.a);
    REQUIRE(count==3);REQUIRE(items.size()==3);
    for(const auto& item:items) {
        REQUIRE(item.clip.x>=100);REQUIRE(item.clip.y>=200);
        REQUIRE(item.clip.x+item.clip.w<=300);REQUIRE(item.clip.y+item.clip.h<=300);
        REQUIRE(item.quad.x<700);
    }
    const auto all=buildDrawList(f.session.document(),{-1000,-1000,4000,4000},kDefaultModeId,items);
    REQUIRE(all==4);
}

TEST_CASE("Starting presentation cancels drafts and props previews without committing them") {
    Prototype f;const auto before=snapshotToJson(f.session.document());
    f.editor.setTool(Tool::Pen);f.editor.pointerDown({1000,1000},PointerButton::Primary);f.editor.pointerUp();
    f.editor.pointerDown({1100,1100},PointerButton::Primary);f.editor.pointerUp();
    REQUIRE(f.editor.drawingPath());REQUIRE(f.editor.startPrototype(f.a,{448,248}));
    REQUIRE_FALSE(f.editor.drawingPath());REQUIRE(snapshotToJson(f.session.document())==before);f.editor.stopPrototype();
    f.editor.select(f.button);NodePatch patch;patch.w=90;REQUIRE(f.editor.previewProps(f.button,patch));
    REQUIRE(f.editor.startPrototype(f.a,{448,248}));REQUIRE_FALSE(f.session.hasPreview());
    REQUIRE(snapshotToJson(f.session.document())==before);REQUIRE(f.editor.selection()==std::vector<Id>{f.button});
    REQUIRE(f.session.takeOutbox().empty());REQUIRE_FALSE(f.session.canUndo());
}


TEST_CASE("Prototype dissolve advances deterministically, blocks clicks and preserves document history") {
    Prototype f;NodePatch p;p.interaction=Interaction{PrototypeAction::Navigate,f.b,PrototypeTransition::Dissolve,400};f.patch(f.button,p);
    f.navigate(f.b,f.a);const auto before=snapshotToJson(f.session.document());
    REQUIRE(f.editor.startPrototype(f.a,{448,248}));f.click(f.center(f.button));
    REQUIRE(f.editor.prototypeScreen()==f.b);REQUIRE(f.editor.prototypeTransitioning());
    auto layers=f.editor.prototypeLayers();REQUIRE(layers.size()==2);
    REQUIRE(layers[0].screen==f.a);REQUIRE(layers[0].opacity==1);REQUIRE(layers[1].opacity==0);
    REQUIRE_THAT(layers[0].offset.x,WithinAbs(600,1e-9));
    f.editor.advancePrototype(100);layers=f.editor.prototypeLayers();
    REQUIRE_THAT(layers[0].opacity,WithinAbs(.75,1e-9));REQUIRE_THAT(layers[1].opacity,WithinAbs(.25,1e-9));
    f.click(f.center(f.b));REQUIRE(f.editor.prototypeScreen()==f.b);
    f.editor.advancePrototype(-5);REQUIRE(f.editor.prototypeTransitioning());
    f.editor.advancePrototype(300);REQUIRE_FALSE(f.editor.prototypeTransitioning());
    REQUIRE(f.editor.prototypeLayers().size()==1);REQUIRE(f.editor.prototypeLayers()[0].opacity==1);
    REQUIRE(f.session.takeOutbox().empty());REQUIRE_FALSE(f.session.canUndo());
    REQUIRE(before==snapshotToJson(f.session.document()));
    REQUIRE(f.editor.prototypeBack());REQUIRE(f.editor.prototypeScreen()==f.a);
}

TEST_CASE("Prototype overlays are centered, modal, independently dismissible and never refit the camera") {
    Prototype f;const auto modal=f.add(NodeType::Frame,{},1100,800,80,60);
    const auto close=f.add(NodeType::Rect,modal,10,10,30,20);NodePatch p;p.interaction=Interaction{PrototypeAction::CloseOverlay,{}};f.patch(close,p);
    p.interaction=Interaction{PrototypeAction::OpenOverlay,modal};f.patch(f.button,p);
    REQUIRE(f.editor.startPrototype(f.a,{448,248}));const auto camera=f.camera;f.click(f.center(f.button));
    REQUIRE(f.editor.prototypeOverlay()==modal);REQUIRE(f.editor.prototypeScreen()==f.a);REQUIRE(f.editor.canPrototypeBack());
    REQUIRE(f.camera.zoom()==camera.zoom());REQUIRE(f.camera.position().x==camera.position().x);
    const auto layers=f.editor.prototypeLayers();REQUIRE(layers.size()==2);REQUIRE(layers[1].overlay);
    REQUIRE_THAT(layers[1].offset.x,WithinAbs(-940,1e-9));REQUIRE_THAT(layers[1].offset.y,WithinAbs(-580,1e-9));
    // Underlying button is blocked; outside clicks dismiss rather than navigate.
    f.click(f.center(f.button));REQUIRE(f.editor.prototypeOverlay().isNil());REQUIRE(f.editor.prototypeScreen()==f.a);
    f.click(f.center(f.button));f.click(f.center(close)+f.editor.prototypeLayers().back().offset);
    REQUIRE(f.editor.prototypeOverlay().isNil());REQUIRE_FALSE(f.editor.canPrototypeBack());
    REQUIRE(f.session.takeOutbox().empty());
}

TEST_CASE("Prototype overlay outside dismissal requires a complete primary click and respects its setting") {
    Prototype f;const auto modal=f.add(NodeType::Frame,{},1100,800,80,60);NodePatch p;
    p.interaction=Interaction{PrototypeAction::OpenOverlay,modal,PrototypeTransition::Instant,300,false};f.patch(f.button,p);
    REQUIRE(f.editor.startPrototype(f.a,{448,248}));f.click(f.center(f.button));
    f.click({110,210});REQUIRE(f.editor.prototypeOverlay()==modal);REQUIRE(f.editor.prototypeBack());
    p.interaction=Interaction{PrototypeAction::OpenOverlay,modal};f.patch(f.button,p);f.click(f.center(f.button));
    const auto outside=f.camera.toScreen({110,210}),inside=f.camera.toScreen({200,250});
    f.editor.pointerDown(inside,PointerButton::Primary);f.editor.pointerMove(outside);f.editor.pointerUp();REQUIRE(f.editor.prototypeOverlay()==modal);
    f.editor.pointerDown(outside,PointerButton::Primary);f.editor.pointerMove({outside.x+5,outside.y});f.editor.pointerMove(outside);f.editor.pointerUp();REQUIRE(f.editor.prototypeOverlay()==modal);
    f.editor.pointerDown(outside,PointerButton::Primary);f.editor.pointerCancel();f.editor.pointerUp();REQUIRE(f.editor.prototypeOverlay()==modal);
    f.click({110,210},PointerButton::Secondary);REQUIRE(f.editor.prototypeOverlay()==modal);
    f.click({110,210});REQUIRE(f.editor.prototypeOverlay().isNil());
}

TEST_CASE("Prototype overlay stack is bounded and Back closes the top before navigation history") {
    Prototype f;std::vector<Id> modals;
    for(int i=0;i<9;++i) modals.push_back(f.add(NodeType::Frame,{},1100+i*100,800,80,60));
    NodePatch p;p.interaction=Interaction{PrototypeAction::OpenOverlay,modals.front()};f.patch(f.b,p);
    for(std::size_t i=0;i+1<modals.size();++i) {p.interaction=Interaction{PrototypeAction::OpenOverlay,modals[i+1]};f.patch(modals[i],p);}
    REQUIRE(f.editor.startPrototype(f.a,{448,248}));f.click(f.center(f.button));REQUIRE(f.editor.prototypeScreen()==f.b);
    f.click(f.center(f.b));
    for(int i=0;i<9;++i) {
        auto layers=f.editor.prototypeLayers();f.click(f.center(layers.back().screen)+layers.back().offset);
    }
    REQUIRE(f.editor.prototypeLayers().size()==9);REQUIRE(f.editor.prototypeOverlay()==modals[7]);
    for(int i=0;i<8;++i) REQUIRE(f.editor.prototypeBack());
    REQUIRE(f.editor.prototypeScreen()==f.b);REQUIRE(f.editor.prototypeOverlay().isNil());
    REQUIRE(f.editor.prototypeBack());REQUIRE(f.editor.prototypeScreen()==f.a);REQUIRE_FALSE(f.editor.canPrototypeBack());
}

TEST_CASE("Prototype dissolve overlays fade only the new modal and Restart clears all temporary state") {
    Prototype f;const auto modal=f.add(NodeType::Frame,{},1100,800,80,60);NodePatch p;
    p.interaction=Interaction{PrototypeAction::OpenOverlay,modal,PrototypeTransition::Dissolve,200};f.patch(f.button,p);
    REQUIRE(f.editor.startPrototype(f.a,{448,248}));f.click(f.center(f.button));
    f.editor.advancePrototype(100);auto layers=f.editor.prototypeLayers();
    REQUIRE(layers.size()==2);REQUIRE(layers[0].opacity==1);REQUIRE(layers[1].opacity==.5);
    REQUIRE(f.editor.restartPrototype());REQUIRE(f.editor.prototypeOverlay().isNil());REQUIRE_FALSE(f.editor.prototypeTransitioning());
    REQUIRE(f.editor.prototypeLayers().size()==1);REQUIRE_FALSE(f.editor.canPrototypeBack());
    f.click(f.center(f.button));REQUIRE(f.editor.prototypeBack());REQUIRE_FALSE(f.editor.prototypeTransitioning());
}

TEST_CASE("Prototype remotely invalid overlays close without ending base presentation") {
    Prototype f;const auto modal=f.add(NodeType::Frame,{},1100,800,80,60);NodePatch p;p.interaction=Interaction{PrototypeAction::OpenOverlay,modal};f.patch(f.button,p);
    REQUIRE(f.editor.startPrototype(f.a,{448,248}));f.click(f.center(f.button));
    p={};p.visible=false;f.patch(modal,p);f.editor.advancePrototype(10);
    REQUIRE(f.editor.presenting());REQUIRE(f.editor.prototypeOverlay().isNil());REQUIRE(f.editor.prototypeLayers().size()==1);
    p={};p.visible=true;f.patch(modal,p);f.click(f.center(f.button));f.editor.stopPrototype();
    REQUIRE(f.editor.prototypeLayers().empty());REQUIRE(f.editor.prototypeOverlay().isNil());
}

TEST_CASE("Prototype navigation from an overlay to its underlying screen clears modals without duplicate history") {
    Prototype f;const auto modal=f.add(NodeType::Frame,{},1100,800,80,60);NodePatch p;
    p.interaction=Interaction{PrototypeAction::OpenOverlay,modal};f.patch(f.button,p);
    p.interaction=Interaction{PrototypeAction::Navigate,f.a,PrototypeTransition::Dissolve,300};f.patch(modal,p);
    REQUIRE(f.editor.startPrototype(f.a,{448,248}));f.click(f.center(f.button));
    f.click(f.center(modal)+f.editor.prototypeLayers().back().offset);
    REQUIRE(f.editor.prototypeScreen()==f.a);REQUIRE(f.editor.prototypeOverlay().isNil());
    REQUIRE_FALSE(f.editor.canPrototypeBack());REQUIRE_FALSE(f.editor.prototypeTransitioning());
    REQUIRE(f.session.takeOutbox().empty());REQUIRE_FALSE(f.session.canUndo());
}


TEST_CASE("Prototype directional slide and push layers have deterministic axis motion and full opacity") {
    const std::array transitions{PrototypeTransition::SlideLeft,PrototypeTransition::SlideRight,PrototypeTransition::SlideUp,PrototypeTransition::SlideDown,
        PrototypeTransition::PushLeft,PrototypeTransition::PushRight,PrototypeTransition::PushUp,PrototypeTransition::PushDown};
    for(std::size_t i=0;i<transitions.size();++i) {
        Prototype f;NodePatch p;p.interaction=Interaction{PrototypeAction::Navigate,f.b,transitions[i],400};f.patch(f.button,p);
        const auto snapshot=snapshotToJson(f.session.document());
        REQUIRE(f.editor.startPrototype(f.a,{448,248}));f.click(f.center(f.button));
        const double dx=i%4==0?200:i%4==1?-200:0,dy=i%4==2?100:i%4==3?-100:0;
        auto layers=f.editor.prototypeLayers();REQUIRE(layers.size()==2);
        REQUIRE(layers[0].opacity==1);REQUIRE(layers[1].opacity==1);
        REQUIRE_THAT(layers[1].offset.x,WithinAbs(dx,1e-9));REQUIRE_THAT(layers[1].offset.y,WithinAbs(dy,1e-9));
        f.editor.advancePrototype(200);layers=f.editor.prototypeLayers();
        REQUIRE_THAT(layers[1].offset.x,WithinAbs(dx/2,1e-9));REQUIRE_THAT(layers[1].offset.y,WithinAbs(dy/2,1e-9));
        REQUIRE_THAT(layers[0].offset.x,WithinAbs(600-(i>=4?dx/2:0),1e-9));
        REQUIRE_THAT(layers[0].offset.y,WithinAbs(i>=4?-dy/2:0,1e-9));
        f.editor.advancePrototype(200);layers=f.editor.prototypeLayers();REQUIRE(layers.size()==1);
        REQUIRE(layers[0].offset.x==0);REQUIRE(layers[0].offset.y==0);REQUIRE_FALSE(f.editor.prototypeTransitioning());
        REQUIRE(snapshotToJson(f.session.document())==snapshot);REQUIRE(f.session.takeOutbox().empty());
    }
}

TEST_CASE("Prototype overlay anchors and manual offsets share placement-aware rendering and hit testing") {
    const std::array placements{OverlayPlacement::TopLeft,OverlayPlacement::TopCenter,OverlayPlacement::TopRight,
        OverlayPlacement::CenterLeft,OverlayPlacement::Center,OverlayPlacement::CenterRight,
        OverlayPlacement::BottomLeft,OverlayPlacement::BottomCenter,OverlayPlacement::BottomRight,OverlayPlacement::Manual};
    for(std::size_t i=0;i<placements.size();++i) {
        Prototype f;const auto modal=f.add(NodeType::Frame,{},1100,800,80,60);NodePatch p;
        p.interaction=Interaction{PrototypeAction::CloseOverlay,{}};f.patch(modal,p);
        Interaction action{PrototypeAction::OpenOverlay,modal};action.placement=placements[i];
        if(i==9) action.overlayOffset={17,23};
        p.interaction=action;f.patch(f.button,p);REQUIRE(f.editor.startPrototype(f.a,{448,248}));f.click(f.center(f.button));
        auto layer=f.editor.prototypeLayers().back();
        const double x=i==9?117:100+(i%3)*60,y=i==9?223:200+(i/3)*20;
        REQUIRE_THAT(1100+layer.offset.x,WithinAbs(x,1e-9));REQUIRE_THAT(800+layer.offset.y,WithinAbs(y,1e-9));
        f.click({x+40,y+30});REQUIRE(f.editor.prototypeOverlay().isNil());
        REQUIRE(f.session.takeOutbox().empty());
    }
}

TEST_CASE("Prototype sliding overlay starts fully beyond the stage and keeps its scrim opacity stable") {
    const std::array transitions{PrototypeTransition::SlideLeft,PrototypeTransition::SlideRight,PrototypeTransition::SlideUp,PrototypeTransition::SlideDown};
    for(std::size_t i=0;i<transitions.size();++i) {
        Prototype f;const auto modal=f.add(NodeType::Frame,{},1100,800,80,60);NodePatch p;
        Interaction action{PrototypeAction::OpenOverlay,modal,transitions[i],400};action.placement=OverlayPlacement::Manual;action.overlayOffset={17,23};
        p.interaction=action;f.patch(f.button,p);REQUIRE(f.editor.startPrototype(f.a,{448,248}));f.click(f.center(f.button));
        auto layers=f.editor.prototypeLayers();auto layer=layers.back();
        const double startX=1100+layer.offset.x,startY=800+layer.offset.y;
        if(i==0) REQUIRE_THAT(startX,WithinAbs(300,1e-9));
        if(i==1) REQUIRE_THAT(startX+80,WithinAbs(100,1e-9));
        if(i==2) REQUIRE_THAT(startY,WithinAbs(300,1e-9));
        if(i==3) REQUIRE_THAT(startY+60,WithinAbs(200,1e-9));
        REQUIRE(layer.opacity==1);REQUIRE(layers.front().offset.x==0);REQUIRE(layers.front().opacity==1);
        f.editor.advancePrototype(200);layer=f.editor.prototypeLayers().back();
        REQUIRE(layer.opacity==1);REQUIRE_THAT(1100+layer.offset.x,WithinAbs((startX+117)/2,1e-9));
        REQUIRE_THAT(800+layer.offset.y,WithinAbs((startY+223)/2,1e-9));
        REQUIRE(f.editor.prototypeBack());REQUIRE_FALSE(f.editor.prototypeTransitioning());REQUIRE(f.editor.prototypeOverlay().isNil());
    }
}

TEST_CASE("Prototype manually positioned modal hotspots outside the base stage remain unclickable") {
    Prototype f;const auto modal=f.add(NodeType::Frame,{},1100,800,80,60);NodePatch p;
    p.interaction=Interaction{PrototypeAction::Navigate,f.b};f.patch(modal,p);
    Interaction action{PrototypeAction::OpenOverlay,modal};action.placement=OverlayPlacement::Manual;action.overlayOffset={180,20};action.dismissOutside=false;
    p.interaction=action;f.patch(f.button,p);REQUIRE(f.editor.startPrototype(f.a,{448,248}));f.click(f.center(f.button));
    f.click({320,250});REQUIRE(f.editor.prototypeScreen()==f.a);REQUIRE(f.editor.prototypeOverlay()==modal);
    f.click({290,250});REQUIRE(f.editor.prototypeScreen()==f.b);REQUIRE(f.editor.prototypeOverlay().isNil());
    REQUIRE(f.editor.prototypeBack());REQUIRE(f.editor.prototypeScreen()==f.a);
    action.dismissOutside=true;p.interaction=action;f.patch(f.button,p);f.click(f.center(f.button));
    f.click({320,250});REQUIRE(f.editor.prototypeScreen()==f.a);REQUIRE(f.editor.prototypeOverlay().isNil());
}

TEST_CASE("Prototype manual overlay placement follows remote base geometry and invalidation clears motion safely") {
    Prototype f;const auto modal=f.add(NodeType::Frame,{},1100,800,80,60);NodePatch p;
    Interaction action{PrototypeAction::OpenOverlay,modal,PrototypeTransition::SlideLeft,400};action.placement=OverlayPlacement::Manual;action.overlayOffset={17,23};
    p.interaction=action;f.patch(f.button,p);REQUIRE(f.editor.startPrototype(f.a,{448,248}));f.click(f.center(f.button));
    f.editor.advancePrototype(100);p={};p.x=200;f.patch(f.a,p);f.editor.advancePrototype(300);
    auto layer=f.editor.prototypeLayers().back();REQUIRE_THAT(1100+layer.offset.x,WithinAbs(217,1e-9));
    p={};p.visible=false;f.patch(modal,p);f.editor.advancePrototype(1);
    REQUIRE(f.editor.prototypeOverlay().isNil());REQUIRE_FALSE(f.editor.prototypeTransitioning());REQUIRE(f.editor.presenting());
    REQUIRE(f.session.takeOutbox().empty());
}
TEST_CASE("Prototype dissolve composes complete scenes once rather than fading individual paints") {
    Prototype f;NodePatch p;p.interaction=Interaction{PrototypeAction::Navigate,f.b,PrototypeTransition::Dissolve,400};f.patch(f.button,p);
    REQUIRE(f.editor.startPrototype(f.a,{448,248}));f.click(f.center(f.button));f.editor.advancePrototype(200);
    const auto scene=f.editor.prototypeScene();REQUIRE(scene.dissolve);REQUIRE(scene.progress==.5);
    REQUIRE(scene.from.size()==1);REQUIRE(scene.to.size()==1);
    REQUIRE(scene.from[0].opacity==1);REQUIRE(scene.to[0].opacity==1);
    REQUIRE(scene.from[0].screen==f.a);REQUIRE(scene.to[0].screen==f.b);
    REQUIRE(f.session.takeOutbox().empty());
}

TEST_CASE("Prototype modal dissolve preserves the full background composition on both sides") {
    Prototype f;const auto a=f.add(NodeType::Frame,{},1100,800,80,60),b=f.add(NodeType::Frame,{},1400,800,60,40);NodePatch p;
    p.interaction=Interaction{PrototypeAction::OpenOverlay,a};f.patch(f.button,p);
    p.interaction=Interaction{PrototypeAction::OpenOverlay,b,PrototypeTransition::Dissolve,400};f.patch(a,p);
    REQUIRE(f.editor.startPrototype(f.a,{448,248}));f.click(f.center(f.button));
    f.click(f.center(a)+f.editor.prototypeLayers().back().offset);f.editor.advancePrototype(200);
    const auto scene=f.editor.prototypeScene();REQUIRE(scene.dissolve);REQUIRE(scene.progress==.5);
    REQUIRE(scene.from.size()==2);REQUIRE(scene.to.size()==3);
    REQUIRE(scene.from[0].screen==f.a);REQUIRE(scene.from[1].screen==a);REQUIRE(scene.to[2].screen==b);
    for(const auto* layers:{&scene.from,&scene.to}) for(const auto& layer:*layers) REQUIRE(layer.opacity==1);
    REQUIRE(f.editor.prototypeBack());REQUIRE_FALSE(f.editor.prototypeScene().dissolve);
}

TEST_CASE("Prototype outgoing screen retains its exact prior viewport across unequal destination sizes") {
    Prototype f;NodePatch p;p.w=100;p.h=300;f.patch(f.b,p);
    p={};p.interaction=Interaction{PrototypeAction::Navigate,f.b,PrototypeTransition::Dissolve,400};f.patch(f.button,p);
    REQUIRE(f.editor.startPrototype(f.a,{448,248}));const auto previous=f.camera;f.click(f.center(f.button));
    const auto layer=f.editor.prototypeScene().from.front();REQUIRE(layer.scale!=1);REQUIRE(layer.viewport.has_value());
    for(const Vec2 point:{Vec2{100,200},Vec2{300,300}}) {
        const auto before=previous.toScreen(point),after=f.camera.toScreen({point.x*layer.scale+layer.offset.x,point.y*layer.scale+layer.offset.y});
        REQUIRE_THAT(after.x,WithinAbs(before.x,1e-9));REQUIRE_THAT(after.y,WithinAbs(before.y,1e-9));
    }
    REQUIRE_THAT(layer.viewport->w*f.camera.zoom(),WithinAbs(200*previous.zoom(),1e-9));
    REQUIRE_THAT(layer.viewport->h*f.camera.zoom(),WithinAbs(100*previous.zoom(),1e-9));
    f.editor.advancePrototype(400);REQUIRE_FALSE(f.editor.prototypeScene().dissolve);REQUIRE(f.editor.prototypeScene().from.empty());
}

TEST_CASE("Prototype reduced motion skips animations and changing the preference settles a live transition") {
    for(const auto transition:{PrototypeTransition::Dissolve,PrototypeTransition::SlideUp,PrototypeTransition::PushLeft}) {
        Prototype f;NodePatch p;p.interaction=Interaction{PrototypeAction::Navigate,f.b,transition,400};f.patch(f.button,p);
        const auto snapshot=snapshotToJson(f.session.document());f.editor.setPrototypeReducedMotion(true);
        REQUIRE(f.editor.startPrototype(f.a,{448,248}));f.click(f.center(f.button));
        REQUIRE(f.editor.prototypeScreen()==f.b);REQUIRE_FALSE(f.editor.prototypeTransitioning());REQUIRE(f.editor.canPrototypeBack());
        REQUIRE(f.editor.prototypeBack());f.editor.setPrototypeReducedMotion(false);f.click(f.center(f.button));REQUIRE(f.editor.prototypeTransitioning());
        f.editor.advancePrototype(100);f.editor.setPrototypeReducedMotion(true);
        REQUIRE_FALSE(f.editor.prototypeTransitioning());REQUIRE(f.editor.prototypeScreen()==f.b);
        REQUIRE(snapshotToJson(f.session.document())==snapshot);REQUIRE(f.session.takeOutbox().empty());REQUIRE_FALSE(f.session.canUndo());
    }
}

TEST_CASE("Prototype viewport and remote geometry interruptions finish at the destination without replay writes") {
    for(const bool resize:{true,false}) {
        Prototype f;NodePatch p;p.interaction=Interaction{PrototypeAction::Navigate,f.b,PrototypeTransition::Dissolve,400};f.patch(f.button,p);
        REQUIRE(f.editor.startPrototype(f.a,{448,248}));f.click(f.center(f.button));f.editor.advancePrototype(100);
        if(resize) f.editor.resizePrototype({600,400});
        else {p={};p.w=260;f.patch(f.b,p);f.editor.advancePrototype(1);}
        REQUIRE(f.editor.presenting());REQUIRE(f.editor.prototypeScreen()==f.b);REQUIRE_FALSE(f.editor.prototypeTransitioning());
        REQUIRE(f.editor.prototypeScene().from.empty());REQUIRE(f.editor.canPrototypeBack());REQUIRE(f.session.takeOutbox().empty());
    }
}
TEST_CASE("Prototype directional transitions between unequal viewports fall back without changing authored actions") {
    for(const auto transition:{PrototypeTransition::SlideLeft,PrototypeTransition::SlideRight,PrototypeTransition::SlideUp,PrototypeTransition::SlideDown,
        PrototypeTransition::PushLeft,PrototypeTransition::PushRight,PrototypeTransition::PushUp,PrototypeTransition::PushDown}) {
        Prototype f;NodePatch p;p.w=100;p.h=300;f.patch(f.b,p);p={};
        p.interaction=Interaction{PrototypeAction::Navigate,f.b,transition,400};f.patch(f.button,p);
        const auto snapshot=snapshotToJson(f.session.document());REQUIRE(f.editor.startPrototype(f.a,{448,248}));f.click(f.center(f.button));
        REQUIRE(f.editor.prototypeScene().dissolve);f.editor.advancePrototype(200);
        REQUIRE(f.editor.prototypeScene().progress==.5);f.editor.advancePrototype(200);
        REQUIRE(f.editor.prototypeScreen()==f.b);REQUIRE_FALSE(f.editor.prototypeTransitioning());
        REQUIRE(snapshotToJson(f.session.document())==snapshot);REQUIRE(f.session.takeOutbox().empty());
    }
}

TEST_CASE("Prototype wheel scroll clamps allowed axes, keeps camera fixed and never writes") {
    Prototype f;const auto content=f.add(NodeType::Rect,f.a,350,240,50,60);
    NodePatch p;p.prototypeOverflow=PrototypeOverflow::Vertical;f.patch(f.a,p);
    const auto before=snapshotToJson(f.session.document());
    REQUIRE(f.editor.startPrototype(f.a,{448,248}));const auto camera=f.camera;
    const auto inside=f.camera.toScreen({150,240});
    f.editor.wheel(inside,100,400);
    REQUIRE(f.editor.prototypeScroll(f.a).x==0);REQUIRE(f.editor.prototypeScroll(f.a).y==50);
    f.editor.wheel(inside,10000);REQUIRE(f.editor.prototypeScroll(f.a).y==200);
    f.editor.wheel({0,0},-10000);REQUIRE(f.editor.prototypeScroll(f.a).y==200);
    f.editor.wheel(inside,-10000);REQUIRE(f.editor.prototypeScroll(f.a).y==0);
    REQUIRE(f.camera.zoom()==camera.zoom());REQUIRE(f.camera.position().y==camera.position().y);
    p.prototypeOverflow=PrototypeOverflow::Both;f.patch(f.a,p);
    f.editor.wheel(inside,10000,10000);
    REQUIRE(f.editor.prototypeScroll(f.a).x==200);REQUIRE(f.editor.prototypeScroll(f.a).y==200);
    p.prototypeOverflow=PrototypeOverflow::Horizontal;f.patch(f.a,p);f.editor.advancePrototype(1);
    REQUIRE(f.editor.prototypeScroll(f.a).x==200);REQUIRE(f.editor.prototypeScroll(f.a).y==0);
    p.prototypeOverflow=PrototypeOverflow::None;f.patch(f.a,p);f.editor.advancePrototype(1);
    REQUIRE(f.editor.prototypeScroll(f.a).x==0);REQUIRE(f.editor.prototypeScroll(f.a).y==0);
    // Only the deliberate remote metadata edits changed this snapshot.
    p.prototypeOverflow=PrototypeOverflow::Vertical;f.patch(f.a,p);
    REQUIRE(snapshotToJson(f.session.document())==before);REQUIRE(f.session.takeOutbox().empty());REQUIRE_FALSE(f.session.canUndo());
}

TEST_CASE("Scrolled hotspots navigate, outgoing scenes capture scroll, Back restores and Restart resets") {
    Prototype f;const auto link=f.add(NodeType::Rect,f.a,10,210,60,40);
    NodePatch p;p.prototypeOverflow=PrototypeOverflow::Vertical;f.patch(f.a,p);
    p={};p.interaction=Interaction{PrototypeAction::Navigate,f.b,PrototypeTransition::Dissolve,300};f.patch(link,p);
    REQUIRE(f.editor.startPrototype(f.a,{448,248}));
    const auto inside=f.camera.toScreen({130,250});f.editor.wheel(inside,300);
    REQUIRE(f.editor.prototypeScroll(f.a).y==150);
    f.click(f.center(link)-Vec2{0,150});REQUIRE(f.editor.prototypeScreen()==f.b);
    REQUIRE(f.editor.prototypeScene().from.front().scroll.y==150);
    REQUIRE_FALSE(f.editor.scrollPrototype(inside,{0,30}));
    REQUIRE(f.editor.prototypeBack());REQUIRE(f.editor.prototypeScroll(f.a).y==150);
    REQUIRE(f.editor.restartPrototype());REQUIRE(f.editor.prototypeScroll(f.a).y==0);
    REQUIRE(f.session.takeOutbox().empty());
}

TEST_CASE("Top modal scroll is independent, blocks background wheel and resets when reopened") {
    Prototype f;f.add(NodeType::Rect,f.a,0,200,20,50);f.add(NodeType::Rect,f.b,0,200,20,50);
    NodePatch p;p.prototypeOverflow=PrototypeOverflow::Vertical;f.patch(f.a,p);f.patch(f.b,p);
    p={};p.w=100;p.h=60;f.patch(f.b,p);
    p={};p.interaction=Interaction{PrototypeAction::OpenOverlay,f.b};f.patch(f.button,p);
    REQUIRE(f.editor.startPrototype(f.a,{448,248}));f.click(f.center(f.button));
    const auto inside=f.camera.toScreen({150,250});f.editor.wheel(inside,100);
    REQUIRE(f.editor.prototypeScroll(f.b).y==50);REQUIRE(f.editor.prototypeScroll(f.a).y==0);
    REQUIRE(f.editor.closePrototypeOverlay());REQUIRE(f.editor.prototypeScroll(f.b).y==0);
    f.click(f.center(f.button));REQUIRE(f.editor.prototypeScroll(f.b).y==0);
    f.editor.wheel(f.camera.toScreen({110,210}),100); // Scrim inside the base, outside the modal.
    REQUIRE(f.editor.prototypeScroll(f.a).y==0);REQUIRE(f.editor.prototypeScroll(f.b).y==0);
}

TEST_CASE("Single touch preserves taps but drag, cancellation and wheel cannot activate hotspots") {
    Prototype f;f.add(NodeType::Rect,f.a,0,200,20,50);
    NodePatch p;p.prototypeOverflow=PrototypeOverflow::Vertical;f.patch(f.a,p);
    REQUIRE(f.editor.startPrototype(f.a,{448,248}));
    const auto point=f.camera.toScreen(f.center(f.button));
    REQUIRE(f.editor.prototypeTouchStart(point));f.editor.prototypeTouchMove(point-Vec2{0,40});f.editor.prototypeTouchEnd();
    REQUIRE(f.editor.prototypeScreen()==f.a);REQUIRE(f.editor.prototypeScroll(f.a).y==20);
    REQUIRE(f.editor.restartPrototype());
    f.editor.prototypeTouchStart(point);f.editor.pointerCancel();f.editor.prototypeTouchEnd();REQUIRE(f.editor.prototypeScreen()==f.a);
    f.editor.pointerDown(point,PointerButton::Primary);f.editor.wheel(point,-20);f.editor.pointerUp();REQUIRE(f.editor.prototypeScreen()==f.a);
    f.editor.prototypeTouchStart(point);f.editor.prototypeTouchEnd();REQUIRE(f.editor.prototypeScreen()==f.b);
}

TEST_CASE("Remote content shrink clamps scroll and cancels in-flight touch; resize preserves clamped position") {
    Prototype f;const auto content=f.add(NodeType::Rect,f.a,0,200,20,50);
    NodePatch p;p.prototypeOverflow=PrototypeOverflow::Vertical;f.patch(f.a,p);
    REQUIRE(f.editor.startPrototype(f.a,{448,248}));auto point=f.camera.toScreen({160,270});
    f.editor.wheel(point,300);REQUIRE(f.editor.prototypeScroll(f.a).y==150);
    f.editor.prototypeTouchStart(point);
    p={};p.y=60;f.patch(content,p);f.editor.advancePrototype(1);
    REQUIRE(f.editor.prototypeScroll(f.a).y==10);
    f.editor.prototypeTouchMove(point-Vec2{0,100});f.editor.prototypeTouchEnd();REQUIRE(f.editor.prototypeScroll(f.a).y==10);
    f.editor.resizePrototype({600,400});REQUIRE(f.editor.prototypeScroll(f.a).y==10);
    f.editor.stopPrototype();REQUIRE(f.editor.startPrototype(f.a,{448,248}));REQUIRE(f.editor.prototypeScroll(f.a).y==0);
}

TEST_CASE("Ordered click entries dispatch only the first match and leave playback history clean") {
    Prototype f;const auto c=f.add(NodeType::Frame,{},1000,200,200,100);
    NodePatch p;p.interactions=std::vector<TriggeredInteraction>{
        {PrototypeTrigger::Click,{PrototypeAction::Navigate,f.b}},
        {PrototypeTrigger::Click,{PrototypeAction::Navigate,c}}};f.patch(f.button,p);
    const auto snapshot=snapshotToJson(f.session.document());
    REQUIRE(f.editor.startPrototype(f.a,{448,248}));f.click(f.center(f.button));
    REQUIRE(f.editor.prototypeScreen()==f.b);REQUIRE(f.editor.prototypeBack());
    p.interactions=std::vector<TriggeredInteraction>{};f.patch(f.button,p); // Remote changes cancel the captured press.
    f.click(f.center(f.button));REQUIRE(f.editor.prototypeScreen()==f.a);
    REQUIRE(f.session.takeOutbox().empty());REQUIRE_FALSE(f.session.canUndo());
}

TEST_CASE("Hover entry resolves ancestors by trigger, excludes drag and touch, and never fires on release") {
    Prototype f;const auto hotspot=f.add(NodeType::Frame,f.a,90,10,60,40);
    const auto child=f.add(NodeType::Rect,hotspot,5,5,10,10);
    NodePatch p;p.interactions=std::vector<TriggeredInteraction>{{PrototypeTrigger::Hover,{PrototypeAction::Navigate,f.b}}};f.patch(hotspot,p);
    REQUIRE(f.editor.startPrototype(f.a,{448,248}));const auto point=f.camera.toScreen(f.center(child));
    f.editor.pointerDown(point,PointerButton::Primary);f.editor.pointerMove(point);f.editor.pointerUpAt(point);
    REQUIRE(f.editor.prototypeScreen()==f.a);
    f.editor.prototypeTouchStart(point);f.editor.prototypeTouchMove(point);f.editor.prototypeTouchEnd();REQUIRE(f.editor.prototypeScreen()==f.a);
    f.editor.pointerMove(point);REQUIRE(f.editor.prototypeScreen()==f.b);
    REQUIRE(f.editor.prototypeBack());f.editor.pointerUpAt(point);REQUIRE(f.editor.prototypeScreen()==f.a);
    f.editor.pointerMove(point);REQUIRE(f.editor.prototypeScreen()==f.b);
}

TEST_CASE("Primary press consumes its click and ignores secondary buttons or transitioning scenes") {
    Prototype f;const auto modal=f.add(NodeType::Frame,{},1000,200,100,60);
    NodePatch p;p.interactions=std::vector<TriggeredInteraction>{
        {PrototypeTrigger::Press,{PrototypeAction::OpenOverlay,modal}},
        {PrototypeTrigger::Click,{PrototypeAction::Navigate,f.b}}};f.patch(f.button,p);
    REQUIRE(f.editor.startPrototype(f.a,{448,248}));const auto point=f.camera.toScreen(f.center(f.button));
    f.editor.pointerDown(point,PointerButton::Secondary);f.editor.pointerUpAt(point);REQUIRE(f.editor.prototypeOverlay().isNil());
    f.editor.pointerDown(point,PointerButton::Primary);REQUIRE(f.editor.prototypeOverlay()==modal);
    f.editor.pointerUpAt(point);REQUIRE(f.editor.prototypeScreen()==f.a);REQUIRE(f.editor.prototypeOverlay()==modal);
    REQUIRE(f.editor.closePrototypeOverlay());p.interactions=std::vector<TriggeredInteraction>{{PrototypeTrigger::Press,{PrototypeAction::Navigate,f.b,PrototypeTransition::Dissolve,300}}};f.patch(f.button,p);
    f.editor.pointerDown(point,PointerButton::Primary);REQUIRE(f.editor.prototypeTransitioning());f.editor.pointerDown(point,PointerButton::Primary);
    REQUIRE(f.editor.prototypeScreen()==f.b);REQUIRE(f.session.takeOutbox().empty());
}

TEST_CASE("Delay runs one first-match action per node and at most one automatic action per frame") {
    Prototype f;NodePatch p;p.interactions=std::vector<TriggeredInteraction>{
        {PrototypeTrigger::Delay,{PrototypeAction::Navigate,f.a},100}, // Consumed no-op.
        {PrototypeTrigger::Delay,{PrototypeAction::Navigate,f.b},1}};f.patch(f.a,p);
    p.interactions=std::vector<TriggeredInteraction>{{PrototypeTrigger::Delay,{PrototypeAction::Navigate,f.b},100}};f.patch(f.button,p);
    REQUIRE(f.editor.startPrototype(f.a,{448,248}));
    f.editor.advancePrototype(1);REQUIRE(f.editor.prototypeScreen()==f.a); // Shadowed timer cannot fire.
    f.editor.advancePrototype(99);REQUIRE(f.editor.prototypeScreen()==f.a); // First frame consumes root's no-op.
    f.editor.advancePrototype(1);REQUIRE(f.editor.prototypeScreen()==f.b); // Next node gets the next frame.
    REQUIRE(f.session.takeOutbox().empty());REQUIRE_FALSE(f.session.canUndo());
}

TEST_CASE("Delay pauses through transitions and cyclic screen timers never recurse in one frame") {
    Prototype f;NodePatch p;p.interactions=std::vector<TriggeredInteraction>{{PrototypeTrigger::Delay,{PrototypeAction::Navigate,f.b,PrototypeTransition::Dissolve,300},100}};f.patch(f.a,p);
    p.interactions=std::vector<TriggeredInteraction>{{PrototypeTrigger::Delay,{PrototypeAction::Back},100}};f.patch(f.b,p);
    const auto snapshot=snapshotToJson(f.session.document());REQUIRE(f.editor.startPrototype(f.a,{448,248}));
    f.editor.advancePrototype(99);REQUIRE(f.editor.prototypeScreen()==f.a);
    f.editor.advancePrototype(1);REQUIRE(f.editor.prototypeScreen()==f.b);REQUIRE(f.editor.prototypeTransitioning());
    f.editor.advancePrototype(10000);REQUIRE(f.editor.prototypeScreen()==f.b);REQUIRE_FALSE(f.editor.prototypeTransitioning());
    f.editor.advancePrototype(99);REQUIRE(f.editor.prototypeScreen()==f.b);f.editor.advancePrototype(1);REQUIRE(f.editor.prototypeScreen()==f.a);
    f.editor.advancePrototype(10000);REQUIRE(f.editor.prototypeScreen()==f.b); // One navigation, not an unbounded cycle.
    f.editor.stopPrototype();f.editor.advancePrototype(10000);REQUIRE_FALSE(f.editor.presenting());
    REQUIRE(snapshotToJson(f.session.document())==snapshot);REQUIRE(f.session.takeOutbox().empty());
}

TEST_CASE("Delay eligibility honors hidden and scrolled clipping, while top modals suspend base timers") {
    Prototype f;NodePatch p;p.interaction=Interaction{};f.patch(f.button,p);
    const auto offscreen=f.add(NodeType::Rect,f.a,10,250,30,30),hidden=f.add(NodeType::Rect,f.a,10,10,20,20);
    p={};p.prototypeOverflow=PrototypeOverflow::Vertical;f.patch(f.a,p);
    p={};p.interactions=std::vector<TriggeredInteraction>{{PrototypeTrigger::Delay,{PrototypeAction::Navigate,f.b},100}};f.patch(offscreen,p);p.visible=false;f.patch(hidden,p);
    REQUIRE(f.editor.startPrototype(f.a,{448,248}));f.editor.advancePrototype(500);REQUIRE(f.editor.prototypeScreen()==f.a);
    f.editor.wheel(f.camera.toScreen({150,250}),10000);f.editor.advancePrototype(1);REQUIRE(f.editor.prototypeScreen()==f.b);
    REQUIRE(f.editor.restartPrototype());
    const auto modal=f.add(NodeType::Frame,{},1000,200,100,60);p={};p.interaction=Interaction{PrototypeAction::OpenOverlay,modal};f.patch(f.button,p);
    p={};p.interactions=std::vector<TriggeredInteraction>{{PrototypeTrigger::Delay,{PrototypeAction::Navigate,f.b},100}};f.patch(f.a,p);
    REQUIRE(f.editor.restartPrototype());f.click(f.center(f.button));REQUIRE(f.editor.prototypeOverlay()==modal);
    f.editor.advancePrototype(1000);REQUIRE(f.editor.prototypeScreen()==f.a);REQUIRE(f.editor.closePrototypeOverlay());
    f.editor.advancePrototype(99);REQUIRE(f.editor.prototypeScreen()==f.a);f.editor.advancePrototype(1);REQUIRE(f.editor.prototypeScreen()==f.b);
}

TEST_CASE("Remote edits cancel delays while viewport resizing restarts a complete delay epoch") {
    for(const bool remote:{false,true}) {
        Prototype f;NodePatch p;p.interactions=std::vector<TriggeredInteraction>{{PrototypeTrigger::Delay,{PrototypeAction::Navigate,f.b},100}};f.patch(f.a,p);
        REQUIRE(f.editor.startPrototype(f.a,{448,248}));f.editor.advancePrototype(50);
        if(remote) {p={};p.name="Changed";f.patch(f.button,p);} else f.editor.resizePrototype({600,400});
        if(remote) {f.editor.advancePrototype(10000);REQUIRE(f.editor.prototypeScreen()==f.a);}
        else {f.editor.advancePrototype(99);REQUIRE(f.editor.prototypeScreen()==f.a);f.editor.advancePrototype(1);REQUIRE(f.editor.prototypeScreen()==f.b);}
        REQUIRE(f.editor.restartPrototype());f.editor.advancePrototype(100);REQUIRE(f.editor.prototypeScreen()==f.b);
    }
}

TEST_CASE("Independent copied-layer trigger lists replace inherited actions in playback") {
    Prototype f;const auto main=f.add(NodeType::Component,{},1000,0,100,60),child=f.add(NodeType::Rect,main,0,0,100,60);
    NodePatch p;p.interactions=std::vector<TriggeredInteraction>{{PrototypeTrigger::Click,{PrototypeAction::Navigate,f.b}}};f.patch(child,p);
    Node instance;instance.id=f.session.newId();instance.type=NodeType::Instance;instance.component=main;instance.parent=f.a;instance.order="a1";instance.x=90;instance.y=10;instance.w=100;instance.h=60;
    REQUIRE(f.session.applyRemote(CreateNode{instance}));
    p.interactions=std::vector<TriggeredInteraction>{{PrototypeTrigger::Hover,{PrototypeAction::Navigate,f.b}}};REQUIRE(f.session.applyRemote(SetOverride{instance.id,child,p}));
    REQUIRE(f.editor.startPrototype(f.a,{448,248}));const Vec2 world{220,240};
    f.click(world);REQUIRE(f.editor.prototypeScreen()==f.a);f.editor.pointerMove(f.camera.toScreen(world));REQUIRE(f.editor.prototypeScreen()==f.b);
    REQUIRE(f.session.takeOutbox().empty());
}

TEST_CASE("Initial full-stage layout settles before delay timing without canceling the flow") {
    Prototype f;NodePatch p;p.interactions=std::vector<TriggeredInteraction>{{PrototypeTrigger::Delay,{PrototypeAction::Navigate,f.b},100}};f.patch(f.a,p);
    REQUIRE(f.editor.startPrototype(f.a,{200,100}));f.editor.advancePrototype(25);f.editor.resizePrototype({870,668});
    f.editor.advancePrototype(99);REQUIRE(f.editor.prototypeScreen()==f.a);
    f.editor.advancePrototype(1);REQUIRE(f.editor.prototypeScreen()==f.b);
}

TEST_CASE("Automatic modal entry cancels an active base touch instead of scrolling behind the scrim") {
    Prototype f;const auto modal=f.add(NodeType::Frame,{},1000,200,100,60);f.add(NodeType::Rect,f.a,0,200,20,100);
    NodePatch p;p.prototypeOverflow=PrototypeOverflow::Vertical;f.patch(f.a,p);
    p={};p.interactions=std::vector<TriggeredInteraction>{{PrototypeTrigger::Delay,{PrototypeAction::OpenOverlay,modal},100}};f.patch(f.a,p);
    REQUIRE(f.editor.startPrototype(f.a,{448,248}));const auto point=f.camera.toScreen({180,260});
    f.editor.prototypeTouchStart(point);f.editor.advancePrototype(100);REQUIRE(f.editor.prototypeOverlay()==modal);
    f.editor.prototypeTouchMove(point-Vec2{0,60});f.editor.prototypeTouchEnd();
    REQUIRE(f.editor.prototypeScroll(f.a).y==0);REQUIRE(f.editor.prototypeScroll(modal).y==0);
}

namespace {
struct InteractivePrototype : Prototype {
    Id main,visual,one,two;
    InteractivePrototype() {
        main=add(NodeType::Component,{},1000,0,90,80);
        visual=add(NodeType::Rect,main,5,5,30,20);
        ComponentDefinition definition;definition.axes.push_back({"state",{"idle","active"},"idle"});
        NodePatch active;active.x=35;active.w=50;active.cornerRadius=8.;active.fill=Oklch{.7,.15,140};
        definition.rules.push_back({{{"state","active"}},visual,active});
        REQUIRE(session.applyRemote(SetComponentDefinition{main,definition}));
        NodePatch action;Interaction interaction;interaction.action=PrototypeAction::ChangeState;interaction.stateValues={{"state","active"}};action.interaction=interaction;patch(main,action);
        one=instance(main,a,0,"a1");two=instance(main,a,100,"a2");
    }
    Id instance(Id component,Id parent,double x,const std::string& order) {
        Node n;n.id=session.newId();n.type=NodeType::Instance;n.component=component;n.parent=parent;n.order=order;n.x=x;n.y=10;n.w=90;n.h=80;
        REQUIRE(session.applyRemote(CreateNode{n}));return n.id;
    }
    Id visualCopy(Id instance) const {
        for(const auto& key:editor.prototypeDocument().viewChildren(instance)) if(editor.prototypeDocument().sourceOf(key)==visual) return key;
        return {};
    }
    bool active(Id instance) const {
        const auto& values=editor.prototypeDocument().find(instance)->componentValues;
        const auto found=values.find("state");return found!=values.end() && std::get<std::string>(found->second)=="active";
    }
};
}

TEST_CASE("Detached playback copies rebuild warmed caches and keep instance states independent of authoring") {
    InteractivePrototype f;const auto before=snapshotToJson(f.session.document());
    const auto* warmed=f.session.document().view(f.one);REQUIRE(warmed);
    REQUIRE(f.editor.startPrototype(f.a,{448,248}));
    REQUIRE(f.editor.prototypeDocument().view(f.one)!=warmed);
    f.click({140,250});REQUIRE(f.active(f.one));REQUIRE_FALSE(f.active(f.two));
    REQUIRE(f.editor.prototypeDocument().box(f.visualCopy(f.one)).x==35);
    REQUIRE(f.editor.prototypeDocument().box(f.visualCopy(f.two)).x==5);
    REQUIRE(f.session.document().find(f.one)->componentValues.empty());
    REQUIRE(snapshotToJson(f.session.document())==before);REQUIRE(f.session.takeOutbox().empty());REQUIRE_FALSE(f.session.canUndo());
    REQUIRE(f.editor.restartPrototype());REQUIRE_FALSE(f.active(f.one));
    f.click({240,250});REQUIRE(f.active(f.two));f.editor.stopPrototype();
    REQUIRE(snapshotToJson(f.session.document())==before);REQUIRE(f.editor.selection().empty());
}

TEST_CASE("Smart component transitions retain immutable scenes and interpolate stable expanded primitives") {
    InteractivePrototype f;NodePatch p;auto action=f.session.document().find(f.main)->interaction;
    action.transition=PrototypeTransition::SmartAnimate;action.durationMs=400;p.interaction=action;f.patch(f.main,p);
    REQUIRE(f.editor.startPrototype(f.a,{448,248}));f.click({140,250});
    auto scene=f.editor.prototypeScene();REQUIRE(scene.smart);REQUIRE_FALSE(scene.from.empty());REQUIRE_FALSE(scene.to.empty());
    REQUIRE(scene.from.front().document->find(f.one)->componentValues.empty());
    REQUIRE(std::get<std::string>(scene.to.front().document->find(f.one)->componentValues.at("state"))=="active");
    f.editor.advancePrototype(200);scene=f.editor.prototypeScene();REQUIRE(scene.smart);REQUIRE(scene.progress==.5);
    std::vector<DrawItem> from,to;const Rect viewport{100,200,200,100};
    buildDrawList(*scene.from.front().document,viewport,kDefaultModeId,from,f.a);
    buildDrawList(*scene.to.front().document,viewport,kDefaultModeId,to,f.a);
    const auto blended=interpolateDrawLists(from,to,.5);REQUIRE(blended.has_value());
    const auto key=f.visualCopy(f.one);
    const auto item=std::find_if(blended->begin(),blended->end(),[&](const auto& paint){return paint.node==key && paint.kind==DrawKind::Fill;});
    REQUIRE(item!=blended->end());REQUIRE(item->quad.x==120);REQUIRE(item->quad.w==40);
    f.editor.advancePrototype(200);REQUIRE_FALSE(f.editor.prototypeTransitioning());REQUIRE(f.active(f.one));
}

TEST_CASE("Temporary variant choices preserve authored properties and independent instance overrides") {
    InteractivePrototype f;
    auto definition=f.session.document().find(f.main)->definition;definition.properties.push_back({"show",f.visual,true});
    REQUIRE(f.session.applyRemote(SetComponentDefinition{f.main,definition}));
    NodePatch p;p.componentValues=ComponentValues{{"show",false}};f.patch(f.one,p);
    p={};p.x=12;REQUIRE(f.session.applyRemote(SetOverride{f.two,f.visual,p}));
    const auto snapshot=snapshotToJson(f.session.document());
    REQUIRE(f.editor.startPrototype(f.a,{448,248}));f.click({140,250});REQUIRE(f.active(f.one));
    REQUIRE_FALSE(std::get<bool>(f.editor.prototypeDocument().find(f.one)->componentValues.at("show")));
    REQUIRE_FALSE(f.editor.prototypeDocument().view(f.visualCopy(f.one))->visible);
    f.click({240,250});REQUIRE(f.active(f.two));REQUIRE(f.editor.prototypeDocument().box(f.visualCopy(f.two)).x==12);
    REQUIRE(snapshotToJson(f.session.document())==snapshot);REQUIRE(f.session.takeOutbox().empty());
}

TEST_CASE("Component state validation fails closed outside an authored instance or for undeclared axis values") {
    InteractivePrototype f;
    for(const auto& values:{std::map<std::string,std::string>{{"missing","active"}},std::map<std::string,std::string>{{"state","missing"}}}) {
        NodePatch p;auto action=f.session.document().find(f.main)->interaction;action.stateValues=values;p.interaction=action;f.patch(f.main,p);
        REQUIRE(f.editor.startPrototype(f.a,{448,248}));f.click({140,250});REQUIRE_FALSE(f.active(f.one));REQUIRE_FALSE(f.editor.prototypeTransitioning());f.editor.stopPrototype();
    }
    const auto outside=f.add(NodeType::Rect,f.a,10,92,30,7);
    NodePatch p;Interaction action;action.action=PrototypeAction::ChangeState;action.stateValues={{"state","active"}};p.interaction=action;f.patch(outside,p);
    REQUIRE(f.editor.startPrototype(f.a,{448,248}));f.click({125,295});REQUIRE_FALSE(f.active(f.one));
    REQUIRE(f.session.takeOutbox().empty());
}

TEST_CASE("Component state persists through navigation and resets on Restart or remote edits") {
    InteractivePrototype f;NodePatch nav;nav.interaction=Interaction{PrototypeAction::Navigate,f.b};f.patch(f.a,nav);
    REQUIRE(f.editor.startPrototype(f.a,{448,248}));f.click({140,250});REQUIRE(f.active(f.one));
    f.click({295,298});REQUIRE(f.editor.prototypeScreen()==f.b);REQUIRE(f.editor.prototypeBack());REQUIRE(f.active(f.one));
    NodePatch remote;remote.name="remote";f.patch(f.visual,remote);f.editor.advancePrototype(1);REQUIRE_FALSE(f.active(f.one));
    f.click({140,250});REQUIRE(f.active(f.one));REQUIRE(f.editor.restartPrototype());REQUIRE_FALSE(f.active(f.one));
}

TEST_CASE("Reduced motion and remote interruption settle component transitions without authoring writes") {
    InteractivePrototype f;NodePatch p;auto action=f.session.document().find(f.main)->interaction;action.transition=PrototypeTransition::SmartAnimate;action.durationMs=400;p.interaction=action;f.patch(f.main,p);
    REQUIRE(f.editor.startPrototype(f.a,{448,248}));f.editor.setPrototypeReducedMotion(true);f.click({140,250});REQUIRE(f.active(f.one));REQUIRE_FALSE(f.editor.prototypeTransitioning());
    REQUIRE(f.editor.restartPrototype());f.editor.setPrototypeReducedMotion(false);f.click({140,250});REQUIRE(f.editor.prototypeTransitioning());
    p={};p.name="updated";f.patch(f.visual,p);f.editor.advancePrototype(1);
    REQUIRE_FALSE(f.editor.prototypeTransitioning());REQUIRE_FALSE(f.active(f.one));REQUIRE(f.session.takeOutbox().empty());
}

TEST_CASE("Derived nested instance state actions cannot mutate the shared authored nested source") {
    InteractivePrototype f;const auto outer=f.add(NodeType::Component,{},1400,0,90,80);
    const auto nested=f.instance(f.main,outer,0,"a0");const auto outerInstance=f.instance(outer,f.a,0,"a3");
    REQUIRE(f.editor.startPrototype(f.a,{448,248}));
    const auto nestedKey=f.editor.prototypeDocument().viewChildren(outerInstance).front();REQUIRE(nestedKey!=nested);
    f.click({140,250});REQUIRE(f.editor.prototypeDocument().find(nested)->componentValues.empty());REQUIRE_FALSE(f.active(f.one));
}
