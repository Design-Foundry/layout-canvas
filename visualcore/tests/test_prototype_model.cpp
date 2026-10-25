#include <catch2/catch_test_macros.hpp>
#include <limits>
#include "vc/editor.hpp"
#include "vc/diagnostics.hpp"
#include "vc/json.hpp"
using namespace vc;
namespace {
struct Prototypes {
    Session session{IdGenerator{146}};Camera camera;Editor editor{session,camera};
    Node node(NodeType type=NodeType::Frame,Id parent={}) {Node n;n.id=session.newId();n.type=type;n.parent=parent;n.order="a0";n.w=200;n.h=120;return n;}
    Node create(NodeType type=NodeType::Frame,Id parent={}){auto n=node(type,parent);REQUIRE(session.edit(CreateNode{n}));return n;}
};
}
TEST_CASE("Prototype JSON patches preserve explicit clears and reject unsupported click actions and fields") {
    const auto clear=patchFromJson(R"({"interaction":null,"prototypeStart":false})");REQUIRE(clear.has_value());REQUIRE(clear->interaction.has_value());REQUIRE(clear->interaction->action==PrototypeAction::None);REQUIRE(clear->prototypeStart.has_value());REQUIRE_FALSE(*clear->prototypeStart);
    const auto back=patchFromJson(R"({"interaction":{"trigger":"click","action":"back"}})");REQUIRE(back.has_value());REQUIRE(back->interaction->action==PrototypeAction::Back);
    for(const auto json:{R"({"interaction":{}})",R"({"interaction":{"trigger":"hover","action":"back"}})",R"({"interaction":{"trigger":"click","action":"back","target":"00000000-0000-0000-0000-000000000001"}})",R"({"interaction":{"trigger":"click","action":"navigate"}})",R"({"interaction":{"trigger":"click","action":"navigate","target":"00000000-0000-0000-0000-000000000000"}})",R"({"interaction":{"trigger":"click","action":"navigate","target":12}})",R"({"interaction":{"trigger":"click","action":"url","target":"https://example.test"}})",R"({"interaction":{"trigger":"click","action":"back","unknown":true}})",R"({"prototypeStart":1})"}) REQUIRE_FALSE(patchFromJson(json).has_value());
    SetProps props{Id{146,1},*clear};const auto encoded=opToJson(props);REQUIRE(encoded.find("\"interaction\":null")!=std::string::npos);REQUIRE(encoded.find("\"prototypeStart\":false")!=std::string::npos);
    const auto decoded=opFromJson("SetProps",encoded);REQUIRE(decoded.has_value());REQUIRE(std::get<SetProps>(*decoded).patch==*clear);
}
TEST_CASE("Prototype interactions and start flags apply merge inverse and undo as independent properties") {
    Prototypes f;const auto a=f.create(),b=f.create();NodePatch click;click.interaction=Interaction{PrototypeAction::Navigate,b.id};REQUIRE(f.editor.setProps(a.id,click));
    NodePatch start;start.prototypeStart=true;REQUIRE(f.editor.setProps(a.id,start));REQUIRE(f.session.document().find(a.id)->prototypeStart);REQUIRE(f.session.document().find(a.id)->interaction.target==b.id);
    const auto combined=mergePatch(click,start);REQUIRE(combined.interaction==click.interaction);REQUIRE(combined.prototypeStart==start.prototypeStart);
    Node n=a;const auto inverse=applyPatch(n,combined);REQUIRE(n.interaction.target==b.id);REQUIRE(n.prototypeStart);applyPatch(n,inverse);REQUIRE(n==a);
    REQUIRE(f.editor.undo());REQUIRE_FALSE(f.session.document().find(a.id)->prototypeStart);REQUIRE(f.session.document().find(a.id)->interaction.target==b.id);REQUIRE(f.editor.redo());
    NodePatch clear;clear.interaction=Interaction{};REQUIRE(f.editor.setProps(a.id,clear));REQUIRE(f.session.document().find(a.id)->interaction.action==PrototypeAction::None);REQUIRE(f.editor.undo());REQUIRE(f.session.document().find(a.id)->interaction.target==b.id);
}
TEST_CASE("Prototype model accepts only authored root Frame targets and authored root Frame start flags") {
    Prototypes f;const auto root=f.create(),source=f.create(NodeType::Rect),child=f.create(NodeType::Frame,root.id),main=f.create(NodeType::Component);const auto instance=*f.editor.createInstance(main.id);
    for(const auto target:{source.id,child.id,main.id,instance,Id{146,999}}) {NodePatch p;p.interaction=Interaction{PrototypeAction::Navigate,target};REQUIRE_FALSE(f.editor.setProps(source.id,p));}
    const auto atomic=f.create();const auto beforePromotion=snapshotToJson(f.session.document());NodePatch promotion;promotion.type=NodeType::Component;promotion.interaction=Interaction{PrototypeAction::Navigate,atomic.id};REQUIRE_FALSE(f.session.edit(SetProps{atomic.id,promotion}));REQUIRE(snapshotToJson(f.session.document())==beforePromotion);
    NodePatch invalid;invalid.interaction=Interaction{PrototypeAction::Back,root.id};REQUIRE_FALSE(f.editor.setProps(source.id,invalid));invalid.interaction=Interaction{static_cast<PrototypeAction>(88),{}};REQUIRE_FALSE(f.editor.setProps(source.id,invalid));
    NodePatch start;start.prototypeStart=true;REQUIRE_FALSE(f.session.edit(SetProps{child.id,start}));REQUIRE_FALSE(f.session.edit(SetProps{main.id,start}));REQUIRE_FALSE(f.session.edit(SetProps{source.id,start}));REQUIRE_FALSE(f.session.edit(SetProps{instance,start}));start.prototypeStart=false;REQUIRE_FALSE(f.session.edit(SetProps{child.id,start}));
    NodePatch click;click.interaction=Interaction{PrototypeAction::Back,{}};REQUIRE_FALSE(f.session.edit(SetProps{instance,click}));click.interaction=Interaction{};REQUIRE_FALSE(f.session.edit(SetProps{instance,click}));
    auto raw=f.node(NodeType::Instance);raw.component=main.id;raw.interaction={PrototypeAction::Back,{}};REQUIRE_FALSE(f.session.edit(CreateNode{raw}));
    auto self=f.node();self.interaction={PrototypeAction::Navigate,self.id};self.prototypeStart=true;REQUIRE_FALSE(f.session.edit(CreateNode{self}));
    self.interaction={};REQUIRE(f.session.edit(CreateNode{self}));NodePatch selfLink;selfLink.interaction=Interaction{PrototypeAction::Navigate,self.id};REQUIRE(f.editor.setProps(self.id,selfLink));REQUIRE(f.editor.undo());REQUIRE(f.editor.undo());REQUIRE(f.session.document().find(self.id)==nullptr);
}
TEST_CASE("Prototype references prevent deleting promoting and nesting their target atomically") {
    Prototypes f;const auto root=f.create(),target=f.create(),source=f.create(NodeType::Rect);NodePatch click;click.interaction=Interaction{PrototypeAction::Navigate,target.id};REQUIRE(f.editor.setProps(source.id,click));const auto before=snapshotToJson(f.session.document());
    REQUIRE_FALSE(f.session.edit(DeleteNode{target.id}));NodePatch promote;promote.type=NodeType::Component;REQUIRE_FALSE(f.session.edit(SetProps{target.id,promote}));REQUIRE_FALSE(f.session.edit(MoveNode{target.id,root.id,"a0"}));REQUIRE(snapshotToJson(f.session.document())==before);
    NodePatch clear;clear.interaction=Interaction{};REQUIRE(f.editor.setProps(source.id,clear));NodePatch start;start.prototypeStart=true;REQUIRE(f.editor.setProps(target.id,start));REQUIRE_FALSE(f.session.edit(MoveNode{target.id,root.id,"a0"}));REQUIRE_FALSE(f.session.edit(SetProps{target.id,promote}));start.prototypeStart=false;REQUIRE(f.editor.setProps(target.id,start));REQUIRE(f.session.edit(MoveNode{target.id,root.id,"a0"}));
}
TEST_CASE("Prototype snapshot16 defers cyclic and forward source links until every frame exists") {
    Prototypes f;const auto a=f.create(),b=f.create();NodePatch p;p.interaction=Interaction{PrototypeAction::Navigate,b.id};p.prototypeStart=true;REQUIRE(f.editor.setProps(a.id,p));p.interaction=Interaction{PrototypeAction::Navigate,a.id};p.prototypeStart=false;REQUIRE(f.editor.setProps(b.id,p));
    const auto json=snapshotToJson(f.session.document());REQUIRE(json.find("\"version\":16")!=std::string::npos);const auto ops=snapshotFromJson(json);REQUIRE(ops.has_value());Document loaded;for(const auto& op:*ops)REQUIRE(loaded.apply(op));REQUIRE(loaded.find(a.id)->interaction.target==b.id);REQUIRE(loaded.find(b.id)->interaction.target==a.id);REQUIRE(loaded.find(a.id)->prototypeStart);
    auto old=json;old.replace(old.find("\"version\":16"),12,"\"version\":15");REQUIRE_FALSE(snapshotFromJson(old).has_value());
    Node plain;plain.id={147,1};plain.order="a0";Document legacy;REQUIRE(legacy.apply(CreateNode{plain}));REQUIRE(snapshotToJson(legacy).find("\"version\":2")!=std::string::npos);
}
TEST_CASE("Component prototype interactions inherit, clear locally and retain independent styles through reload") {
    Prototypes f;const auto target=f.create(),main=f.create(NodeType::Component),label=f.create(NodeType::Rect,main.id);NodePatch click;click.interaction=Interaction{PrototypeAction::Navigate,target.id};REQUIRE(f.editor.setProps(label.id,click));REQUIRE(f.editor.setProps(main.id,click));const auto instance=*f.editor.createInstance(main.id);const auto copy=f.session.document().viewChildren(instance).front();REQUIRE(f.session.document().view(copy)->interaction.target==target.id);
    NodePatch local;local.interaction=Interaction{};local.opacity=.4;REQUIRE(f.session.edit(SetOverride{instance,label.id,local}));REQUIRE(f.session.document().view(copy)->interaction.action==PrototypeAction::None);REQUIRE(std::get<double>(f.session.document().view(copy)->opacity)==.4);
    NodePatch back;back.interaction=Interaction{PrototypeAction::Back,{}};REQUIRE(f.editor.setProps(label.id,back));REQUIRE(f.session.document().view(copy)->interaction.action==PrototypeAction::None);
    NodePatch rootClear;rootClear.interaction=Interaction{};REQUIRE(f.session.edit(SetOverride{instance,main.id,rootClear}));REQUIRE(f.session.document().view(instance)->interaction.action==PrototypeAction::None);
    const auto json=snapshotToJson(f.session.document());REQUIRE(json.find("\"interaction\":null")!=std::string::npos);const auto ops=snapshotFromJson(json);REQUIRE(ops.has_value());Document loaded;for(const auto& op:*ops)REQUIRE(loaded.apply(op));REQUIRE(loaded.view(copy)->interaction.action==PrototypeAction::None);REQUIRE(std::get<double>(loaded.view(copy)->opacity)==.4);
    auto old=json;old.replace(old.find("\"version\":16"),12,"\"version\":15");REQUIRE_FALSE(snapshotFromJson(old).has_value());
    REQUIRE(f.session.edit(SetOverride{instance,label.id,NodePatch{}}));REQUIRE(f.session.document().view(copy)->interaction.action==PrototypeAction::Back);REQUIRE(f.editor.undo());REQUIRE(f.session.document().view(copy)->interaction.action==PrototypeAction::None);
}
TEST_CASE("Prototype override targets protect frames, reject start metadata and forbid variant-authored behavior") {
    Prototypes f;const auto target=f.create(),other=f.create(),main=f.create(NodeType::Component),label=f.create(NodeType::Rect,main.id);const auto instance=*f.editor.createInstance(main.id);NodePatch p;p.interaction=Interaction{PrototypeAction::Navigate,target.id};REQUIRE(f.session.edit(SetOverride{instance,label.id,p}));REQUIRE_FALSE(f.session.edit(DeleteNode{target.id}));REQUIRE_FALSE(f.session.edit(MoveNode{target.id,other.id,"a0"}));
    REQUIRE_FALSE(f.session.edit(SetOverride{instance,Id{146,999},p}));p={};p.prototypeStart=false;REQUIRE_FALSE(f.session.edit(SetOverride{instance,label.id,p}));
    ComponentDefinition def;def.axes.push_back({"state",{"a","b"},"a"});p={};p.interaction=Interaction{};def.rules={{{{"state","b"}},label.id,p}};REQUIRE_FALSE(f.editor.setComponentDefinition(main.id,def));p={};p.prototypeStart=false;def.rules[0].patch=p;REQUIRE_FALSE(f.editor.setComponentDefinition(main.id,def));
    REQUIRE(f.session.edit(SetOverride{instance,label.id,NodePatch{}}));REQUIRE(f.session.edit(DeleteNode{target.id}));
}

TEST_CASE("Prototype overlay and dissolve patches round trip with bounded canonical settings") {
    const auto overlay=patchFromJson(R"({"interaction":{"trigger":"click","action":"open-overlay","target":"00000000-0000-0000-0000-000000000001","transition":"dissolve","durationMs":450,"dismissOutside":false}})");
    REQUIRE(overlay);REQUIRE(overlay->interaction->action==PrototypeAction::OpenOverlay);REQUIRE(overlay->interaction->transition==PrototypeTransition::Dissolve);REQUIRE(overlay->interaction->durationMs==450);REQUIRE_FALSE(overlay->interaction->dismissOutside);
    const SetProps props{Id{146,1},*overlay};const auto json=opToJson(props);const auto decoded=opFromJson("SetProps",json);REQUIRE(decoded);REQUIRE(std::get<SetProps>(*decoded).patch==*overlay);
    const auto defaultDuration=patchFromJson(R"({"interaction":{"trigger":"click","action":"navigate","target":"00000000-0000-0000-0000-000000000001","transition":"dissolve"}})");REQUIRE(defaultDuration);REQUIRE(defaultDuration->interaction->durationMs==300);
    const auto instant=patchFromJson(R"({"interaction":{"trigger":"click","action":"navigate","target":"00000000-0000-0000-0000-000000000001","transition":"instant"}})");REQUIRE(instant);REQUIRE(opToJson(SetProps{Id{146,1},*instant}).find("transition")==std::string::npos);
    const auto close=patchFromJson(R"({"interaction":{"trigger":"click","action":"close-overlay"}})");REQUIRE(close);REQUIRE(close->interaction->action==PrototypeAction::CloseOverlay);REQUIRE(opFromJson("SetProps",opToJson(SetProps{Id{146,1},*close})));
    for(const auto invalid:{
        R"({"interaction":{"trigger":"click","action":"open-overlay"}})",
        R"({"interaction":{"trigger":"click","action":"close-overlay","target":"00000000-0000-0000-0000-000000000001"}})",
        R"({"interaction":{"trigger":"click","action":"back","transition":"dissolve"}})",
        R"({"interaction":{"trigger":"click","action":"navigate","target":"00000000-0000-0000-0000-000000000001","dismissOutside":true}})",
        R"({"interaction":{"trigger":"click","action":"navigate","target":"00000000-0000-0000-0000-000000000001","durationMs":300}})",
        R"({"interaction":{"trigger":"click","action":"navigate","target":"00000000-0000-0000-0000-000000000001","transition":"instant","durationMs":300}})",
        R"({"interaction":{"trigger":"click","action":"navigate","target":"00000000-0000-0000-0000-000000000001","transition":"slide"}})",
        R"({"interaction":{"trigger":"click","action":"navigate","target":"00000000-0000-0000-0000-000000000001","transition":"dissolve","durationMs":0}})",
        R"({"interaction":{"trigger":"click","action":"navigate","target":"00000000-0000-0000-0000-000000000001","transition":"dissolve","durationMs":2001}})",
        R"({"interaction":{"trigger":"click","action":"navigate","target":"00000000-0000-0000-0000-000000000001","transition":"dissolve","durationMs":2.5}})",
        R"({"interaction":{"trigger":"click","action":"open-overlay","target":"00000000-0000-0000-0000-000000000001","dismissOutside":0}})",
        R"({"interaction":{"trigger":"click","action":"open-overlay","target":"00000000-0000-0000-0000-000000000001","position":"center"}})"
    }) REQUIRE_FALSE(patchFromJson(invalid));
}

TEST_CASE("Prototype direct native settings refuse malformed transitions atomically") {
    Prototypes f;const auto source=f.create(),target=f.create();const auto before=snapshotToJson(f.session.document());
    for(const auto invalid:{
        Interaction{PrototypeAction::Navigate,target.id,PrototypeTransition::Instant,200},
        Interaction{PrototypeAction::Navigate,target.id,PrototypeTransition::Dissolve,0},
        Interaction{PrototypeAction::Navigate,target.id,PrototypeTransition::Dissolve,2001},
        Interaction{PrototypeAction::Navigate,target.id,static_cast<PrototypeTransition>(99)},
        Interaction{PrototypeAction::Navigate,target.id,PrototypeTransition::Instant,300,false},
        Interaction{PrototypeAction::Back,{},PrototypeTransition::Dissolve},
        Interaction{PrototypeAction::CloseOverlay,{},PrototypeTransition::Dissolve},
        Interaction{PrototypeAction::None,{},PrototypeTransition::Instant,300,false}
    }) {NodePatch p;p.interaction=invalid;REQUIRE_FALSE(f.editor.setProps(source.id,p));REQUIRE(snapshotToJson(f.session.document())==before);}
}

TEST_CASE("Overlay references protect root frames and snapshot17 retains extended copied interactions") {
    Prototypes f;const auto root=f.create(),target=f.create(),main=f.create(NodeType::Component),hotspot=f.create(NodeType::Rect,main.id),nested=f.create(NodeType::Frame,root.id);const auto instance=*f.editor.createInstance(main.id);
    NodePatch overlay;overlay.interaction=Interaction{PrototypeAction::OpenOverlay,target.id,PrototypeTransition::Dissolve,650,false};REQUIRE(f.editor.setProps(hotspot.id,overlay));REQUIRE_FALSE(f.session.edit(DeleteNode{target.id}));REQUIRE_FALSE(f.session.edit(MoveNode{target.id,root.id,"a0"}));
    NodePatch invalid=overlay;invalid.interaction->target=nested.id;REQUIRE_FALSE(f.editor.setProps(hotspot.id,invalid));invalid.interaction->target=main.id;REQUIRE_FALSE(f.editor.setProps(hotspot.id,invalid));NodePatch promote;promote.type=NodeType::Component;REQUIRE_FALSE(f.editor.setProps(target.id,promote));
    NodePatch local;local.interaction=Interaction{PrototypeAction::CloseOverlay,{}};REQUIRE(f.session.edit(SetOverride{instance,hotspot.id,local}));const auto copy=f.session.document().viewChildren(instance).front();REQUIRE(f.session.document().view(copy)->interaction.action==PrototypeAction::CloseOverlay);
    const auto json=snapshotToJson(f.session.document());REQUIRE(json.find("\"version\":17")!=std::string::npos);const auto ops=snapshotFromJson(json);REQUIRE(ops.has_value());Document loaded;for(const auto& op:*ops)REQUIRE(loaded.apply(op));REQUIRE(loaded.find(hotspot.id)->interaction==*overlay.interaction);REQUIRE(loaded.view(copy)->interaction==*local.interaction);
    auto old=json;old.replace(old.find("\"version\":17"),12,"\"version\":16");REQUIRE_FALSE(snapshotFromJson(old).has_value());
    NodePatch clear;clear.interaction=Interaction{};REQUIRE(f.editor.setProps(hotspot.id,clear));REQUIRE(f.session.edit(DeleteNode{target.id}));REQUIRE(snapshotToJson(f.session.document()).find("\"version\":17")!=std::string::npos);
}

TEST_CASE("Dissolve-only and override-only prototype settings require snapshot17 while instant stays16") {
    Prototypes f;const auto source=f.create(),target=f.create(),main=f.create(NodeType::Component),hotspot=f.create(NodeType::Rect,main.id);const auto instance=*f.editor.createInstance(main.id);
    NodePatch p;p.interaction=Interaction{PrototypeAction::Navigate,target.id};REQUIRE(f.editor.setProps(source.id,p));REQUIRE(snapshotToJson(f.session.document()).find("\"version\":16")!=std::string::npos);
    p.interaction->transition=PrototypeTransition::Dissolve;REQUIRE(f.editor.setProps(source.id,p));auto json=snapshotToJson(f.session.document());REQUIRE(json.find("\"version\":17")!=std::string::npos);auto old=json;old.replace(old.find("\"version\":17"),12,"\"version\":16");REQUIRE_FALSE(snapshotFromJson(old).has_value());
    NodePatch clear;clear.interaction=Interaction{};REQUIRE(f.editor.setProps(source.id,clear));p.interaction=Interaction{PrototypeAction::OpenOverlay,target.id};REQUIRE(f.session.edit(SetOverride{instance,hotspot.id,p}));json=snapshotToJson(f.session.document());REQUIRE(json.find("\"version\":17")!=std::string::npos);old=json;old.replace(old.find("\"version\":17"),12,"\"version\":16");REQUIRE_FALSE(snapshotFromJson(old).has_value());
}

TEST_CASE("Prototype guards retain structured refusal diagnostics after upstream integration") {
    Prototypes f;const auto root=f.create(),source=f.create(NodeType::Rect),target=f.create(),main=f.create(NodeType::Component),hotspot=f.create(NodeType::Rect,main.id);const auto instance=*f.editor.createInstance(main.id);
    const auto refusedWith=[&](const OpBody& op,std::string_view message) {
        const auto before=snapshotToJson(f.session.document());clearDiagnostic();
        REQUIRE_FALSE(f.session.edit(op));REQUIRE(lastDiagnostic());REQUIRE(lastDiagnostic()->message==message);REQUIRE(snapshotToJson(f.session.document())==before);
    };
    NodePatch p;p.interaction=Interaction{PrototypeAction::Navigate,target.id,PrototypeTransition::Dissolve,0};
    refusedWith(SetProps{source.id,p},"interaction has an invalid action, target, transition, duration or outside-dismiss setting");
    p.interaction=Interaction{PrototypeAction::OpenOverlay,source.id};refusedWith(SetProps{hotspot.id,p},"a prototype destination must be an existing root frame");
    p.interaction=Interaction{PrototypeAction::OpenOverlay,target.id};REQUIRE(f.editor.setProps(source.id,p));
    refusedWith(DeleteNode{target.id},"prototype interactions refer to this frame; remove their links first");
    refusedWith(MoveNode{target.id,root.id,"a0"},"a prototype starting or referenced frame must stay at the document root");
    NodePatch promote;promote.type=NodeType::Component;refusedWith(SetProps{target.id,promote},"a prototype starting or referenced frame cannot change its type");
    p={};p.prototypeStart=false;refusedWith(SetOverride{instance,hotspot.id,p},"overrides cannot set prototypeStart");
    p={};p.interaction=Interaction{PrototypeAction::Back,{}};refusedWith(SetOverride{instance,Id{146,999},p},"an interaction override targets the instance root or an existing copied layer");
    REQUIRE(f.session.edit(SetOverride{instance,main.id,p}));
}

TEST_CASE("Slide and push interactions round trip all directions with bounded durations") {
    const std::vector<std::pair<PrototypeTransition,std::string>> transitions{
        {PrototypeTransition::SlideLeft,"slide-left"},{PrototypeTransition::SlideRight,"slide-right"},
        {PrototypeTransition::SlideUp,"slide-up"},{PrototypeTransition::SlideDown,"slide-down"},
        {PrototypeTransition::PushLeft,"push-left"},{PrototypeTransition::PushRight,"push-right"},
        {PrototypeTransition::PushUp,"push-up"},{PrototypeTransition::PushDown,"push-down"}};
    for(const auto& [transition,name]:transitions) {
        const auto patch=patchFromJson(std::string{R"({"interaction":{"trigger":"click","action":"navigate","target":"00000000-0000-0000-0000-000000000001","transition":")"}+name+R"(","durationMs":2000}})");
        REQUIRE(patch);REQUIRE(patch->interaction->transition==transition);REQUIRE(patch->interaction->durationMs==2000);
        const auto encoded=opToJson(SetProps{Id{146,1},*patch});const auto decoded=opFromJson("SetProps",encoded);REQUIRE(decoded);REQUIRE(std::get<SetProps>(*decoded).patch==*patch);
        const auto overlay=patchFromJson(std::string{R"({"interaction":{"trigger":"click","action":"open-overlay","target":"00000000-0000-0000-0000-000000000001","transition":")"}+name+R"("}})");
        if(transition>=PrototypeTransition::PushLeft) REQUIRE_FALSE(overlay);
        else {REQUIRE(overlay);REQUIRE(overlay->interaction->durationMs==300);}
    }
}

TEST_CASE("Overlay anchors and manual offsets round trip with canonical default omission") {
    const std::vector<std::pair<OverlayPlacement,std::string>> placements{
        {OverlayPlacement::Center,"center"},{OverlayPlacement::TopLeft,"top-left"},{OverlayPlacement::TopCenter,"top-center"},
        {OverlayPlacement::TopRight,"top-right"},{OverlayPlacement::CenterLeft,"center-left"},{OverlayPlacement::CenterRight,"center-right"},
        {OverlayPlacement::BottomLeft,"bottom-left"},{OverlayPlacement::BottomCenter,"bottom-center"},{OverlayPlacement::BottomRight,"bottom-right"}};
    for(const auto& [placement,name]:placements) {
        const auto patch=patchFromJson(std::string{R"({"interaction":{"trigger":"click","action":"open-overlay","target":"00000000-0000-0000-0000-000000000001","placement":")"}+name+R"("}})");
        REQUIRE(patch);REQUIRE(patch->interaction->placement==placement);REQUIRE(patch->interaction->overlayOffset==OverlayOffset{});
        const auto encoded=opToJson(SetProps{Id{146,1},*patch});const auto decoded=opFromJson("SetProps",encoded);REQUIRE(decoded);REQUIRE(std::get<SetProps>(*decoded).patch==*patch);
        if(placement==OverlayPlacement::Center) REQUIRE(encoded.find("placement")==std::string::npos);
    }
    const auto manual=patchFromJson(R"({"interaction":{"trigger":"click","action":"open-overlay","target":"00000000-0000-0000-0000-000000000001","placement":"manual","offset":{"x":-100000,"y":100000}}})");
    REQUIRE(manual);REQUIRE(manual->interaction->placement==OverlayPlacement::Manual);REQUIRE(manual->interaction->overlayOffset==OverlayOffset{-100000,100000});
    const auto decoded=opFromJson("SetProps",opToJson(SetProps{Id{146,1},*manual}));REQUIRE(decoded);REQUIRE(std::get<SetProps>(*decoded).patch==*manual);
}

TEST_CASE("Positioned prototype JSON refuses misplaced offsets and produces scoped diagnostics") {
    const auto overlay=[](std::string options){return std::string{R"({"interaction":{"trigger":"click","action":"open-overlay","target":"00000000-0000-0000-0000-000000000001",)"}+options+"}}";};
    const std::vector<std::pair<std::string,std::string>> invalid{
        {R"("placement":"manual")","interaction.offset"},
        {R"("placement":"center","offset":{"x":0,"y":0})","interaction.offset"},
        {R"("offset":{"x":0,"y":0})","interaction.offset"},
        {R"("placement":"free")","interaction.placement"},
        {R"("placement":"manual","offset":{"x":100001,"y":0})","interaction.offset.x"},
        {R"("placement":"manual","offset":{"x":0,"y":-100001})","interaction.offset.y"},
        {R"("placement":"manual","offset":{"x":"2","y":0})","interaction.offset.x"},
        {R"("placement":"manual","offset":{"x":0})","interaction.offset"},
        {R"("placement":"manual","offset":{"x":0,"y":0,"z":0})","interaction.offset"},
        {R"("transition":"push-left")","interaction.transition"},
        {R"("transition":"slide-left","durationMs":0)","interaction.durationMs"},
        {R"("transition":"slide-left","durationMs":2001)","interaction.durationMs"},
        {R"("transition":"slide-left","durationMs":10.5)","interaction.durationMs"}};
    for(const auto& [options,path]:invalid) {clearDiagnostic();REQUIRE_FALSE(patchFromJson(overlay(options)));REQUIRE(lastDiagnostic());REQUIRE(lastDiagnostic()->path==path);}
    REQUIRE_FALSE(patchFromJson(R"({"interaction":{"trigger":"click","action":"navigate","target":"00000000-0000-0000-0000-000000000001","placement":"center"}})"));
    REQUIRE_FALSE(patchFromJson(R"({"interaction":{"trigger":"click","action":"navigate","target":"00000000-0000-0000-0000-000000000001","offset":{"x":0,"y":0}}})"));
}

TEST_CASE("Direct native positioned interactions enforce finite offsets and action compatibility") {
    Prototypes f;const auto source=f.create(),target=f.create();const auto before=snapshotToJson(f.session.document());
    Interaction valid{PrototypeAction::OpenOverlay,target.id,PrototypeTransition::SlideDown,1};valid.placement=OverlayPlacement::Manual;valid.overlayOffset={-100000,100000};REQUIRE(validInteractionSettings(valid));
    for(int invalid=0;invalid<8;++invalid) {
        auto interaction=valid;
        if(invalid==0) interaction.overlayOffset.x=std::numeric_limits<double>::infinity();
        if(invalid==1) interaction.overlayOffset.y=std::numeric_limits<double>::quiet_NaN();
        if(invalid==2) interaction.overlayOffset.x=100001;
        if(invalid==3) interaction.placement=OverlayPlacement::TopLeft;
        if(invalid==4) interaction.placement=static_cast<OverlayPlacement>(99);
        if(invalid==5) interaction.action=PrototypeAction::Navigate;
        if(invalid==6) {interaction.overlayOffset={};interaction.placement=OverlayPlacement::Center;interaction.transition=PrototypeTransition::PushRight;}
        if(invalid==7) interaction.transition=static_cast<PrototypeTransition>(99);
        NodePatch p;p.interaction=interaction;REQUIRE_FALSE(f.editor.setProps(source.id,p));REQUIRE(snapshotToJson(f.session.document())==before);
    }
}

TEST_CASE("Snapshot18 positions and slide transitions guard source and override-only downgrades") {
    Prototypes f;const auto source=f.create(),target=f.create(),main=f.create(NodeType::Component),hotspot=f.create(NodeType::Rect,main.id);const auto instance=*f.editor.createInstance(main.id);
    const auto refusesDowngrade=[&] {
        const auto json=snapshotToJson(f.session.document());REQUIRE(json.find("\"version\":18")!=std::string::npos);
        const auto ops=snapshotFromJson(json);REQUIRE(ops.has_value());Document loaded;for(const auto& op:*ops)REQUIRE(loaded.apply(op));REQUIRE(snapshotToJson(loaded)==json);
        auto old=json;old.replace(old.find("\"version\":18"),12,"\"version\":17");clearDiagnostic();REQUIRE_FALSE(snapshotFromJson(old).has_value());REQUIRE(lastDiagnostic());REQUIRE(lastDiagnostic()->message=="slide, push and positioned overlay interactions need snapshot version 18 or later");
    };
    NodePatch p;p.interaction=Interaction{PrototypeAction::Navigate,target.id,PrototypeTransition::PushUp,450};REQUIRE(f.editor.setProps(source.id,p));refusesDowngrade();
    NodePatch clear;clear.interaction=Interaction{};REQUIRE(f.editor.setProps(source.id,clear));p.interaction=Interaction{PrototypeAction::OpenOverlay,target.id};p.interaction->placement=OverlayPlacement::BottomRight;REQUIRE(f.session.edit(SetOverride{instance,hotspot.id,p}));refusesDowngrade();
    p.interaction->placement=OverlayPlacement::Manual;p.interaction->overlayOffset={-12.5,40};REQUIRE(f.session.edit(SetOverride{instance,hotspot.id,p}));refusesDowngrade();
    p.interaction->placement=OverlayPlacement::Center;p.interaction->overlayOffset={};REQUIRE(f.session.edit(SetOverride{instance,hotspot.id,p}));REQUIRE(snapshotToJson(f.session.document()).find("\"version\":17")!=std::string::npos);
}
