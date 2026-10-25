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
TEST_CASE("Ordered prototype interactions validate triggers delay bounds lists and exclusive legacy actions") {
    const auto patch=patchFromJson(R"({"interactions":[{"trigger":"hover","action":"back"},{"trigger":"press","action":"close-overlay"},{"trigger":"delay","delayMs":60000,"action":"back"},{"trigger":"click","action":"back"}]})");
    REQUIRE(patch.has_value());REQUIRE(patch->interactions->size()==4);
    REQUIRE(patch->interactions->at(0).trigger==PrototypeTrigger::Hover);REQUIRE(patch->interactions->at(2).delayMs==60000);
    const auto encoded=opToJson(SetProps{Id{146,99},*patch});const auto op=opFromJson("SetProps",encoded);REQUIRE(op.has_value());REQUIRE(std::get<SetProps>(*op).patch==*patch);
    for(const auto json:{R"({"interactions":null})",R"({"interactions":{}})",R"({"interactions":[null]})",R"({"interactions":[{"trigger":"delay","action":"back"}]})",R"({"interactions":[{"trigger":"delay","delayMs":0,"action":"back"}]})",R"({"interactions":[{"trigger":"delay","delayMs":60001,"action":"back"}]})",R"({"interactions":[{"trigger":"delay","delayMs":1.5,"action":"back"}]})",R"({"interactions":[{"trigger":"hover","delayMs":5,"action":"back"}]})",R"({"interactions":[{"trigger":"keydown","action":"back"}]})",R"({"interactions":[{"trigger":"click","action":"none"}]})",R"({"interaction":{"trigger":"click","action":"back"},"interactions":[{"trigger":"hover","action":"back"}]})"}) REQUIRE_FALSE(patchFromJson(json).has_value());
    NodePatch oversized;oversized.interactions=std::vector<TriggeredInteraction>(17,{PrototypeTrigger::Click,Interaction{PrototypeAction::Back,{}}});Prototypes f;const auto source=f.create();REQUIRE_FALSE(f.editor.setProps(source.id,oversized));
    for(const auto entry:{TriggeredInteraction{PrototypeTrigger::Delay,Interaction{PrototypeAction::Back,{}},0},TriggeredInteraction{PrototypeTrigger::Hover,Interaction{PrototypeAction::Back,{}},2},TriggeredInteraction{static_cast<PrototypeTrigger>(99),Interaction{PrototypeAction::Back,{}}},TriggeredInteraction{PrototypeTrigger::Click,Interaction{}}}) {
        NodePatch malformed;malformed.interactions=std::vector<TriggeredInteraction>{entry};REQUIRE_FALSE(f.editor.setProps(source.id,malformed));
    }
}

TEST_CASE("Ordered interactions atomically migrate legacy state with reversible independent patches") {
    Prototypes f;const auto source=f.create(),target=f.create();NodePatch legacy;legacy.interaction=Interaction{PrototypeAction::Navigate,target.id};REQUIRE(f.editor.setProps(source.id,legacy));
    NodePatch list;list.interactions=std::vector<TriggeredInteraction>{{PrototypeTrigger::Hover,Interaction{PrototypeAction::Back,{}}},{PrototypeTrigger::Delay,Interaction{PrototypeAction::Navigate,target.id},250}};
    REQUIRE(f.editor.setProps(source.id,list));REQUIRE(f.session.document().find(source.id)->interaction==Interaction{});REQUIRE(f.session.document().find(source.id)->interactions.size()==2);
    REQUIRE(f.editor.undo());REQUIRE(f.session.document().find(source.id)->interaction==*legacy.interaction);REQUIRE(f.session.document().find(source.id)->interactions.empty());REQUIRE(f.editor.redo());
    REQUIRE(f.editor.setProps(source.id,legacy));REQUIRE(f.session.document().find(source.id)->interactions.empty());REQUIRE(f.editor.undo());REQUIRE(f.session.document().find(source.id)->interactions==*list.interactions);
    Node node=source;applyPatch(node,legacy);const auto inverse=applyPatch(node,list);applyPatch(node,inverse);REQUIRE(node.interaction==*legacy.interaction);REQUIRE(node.interactions.empty());
    const auto merged=mergePatch(legacy,list);REQUIRE(merged.interaction->action==PrototypeAction::None);REQUIRE(merged.interactions==list.interactions);
    REQUIRE_FALSE(mergePatch(NodePatch{},legacy).interactions.has_value());
}

TEST_CASE("Ordered snapshot20 preserves forward cyclic references and rejects explicit downlevel lists") {
    Prototypes f;const auto a=f.create(),b=f.create();NodePatch p;p.interactions=std::vector<TriggeredInteraction>{{PrototypeTrigger::Delay,Interaction{PrototypeAction::Navigate,b.id},10}};REQUIRE(f.editor.setProps(a.id,p));p.interactions->at(0).interaction.target=a.id;REQUIRE(f.editor.setProps(b.id,p));
    const auto snapshot=snapshotToJson(f.session.document());REQUIRE(snapshot.find("\"version\":20")!=std::string::npos);const auto ops=snapshotFromJson(snapshot);REQUIRE(ops.has_value());Document loaded;for(const auto& op:*ops) REQUIRE(loaded.apply(op));REQUIRE(loaded.find(a.id)->interactions.front().interaction.target==b.id);REQUIRE(loaded.find(b.id)->interactions.front().interaction.target==a.id);
    auto old=snapshot;old.replace(old.find("\"version\":20"),12,"\"version\":19");REQUIRE_FALSE(snapshotFromJson(old).has_value());
    REQUIRE_FALSE(snapshotFromJson(R"({"version":19,"modes":[],"tokens":[],"nodes":[{"id":"00000000-0000-0000-0000-000000000001","type":"frame","order":"a0","interactions":[]}]})").has_value());
}

TEST_CASE("Every ordered target protects its root frame even when another trigger comes first") {
    Prototypes f;const auto source=f.create(NodeType::Rect),target=f.create(),container=f.create();NodePatch p;p.interactions=std::vector<TriggeredInteraction>{{PrototypeTrigger::Click,Interaction{PrototypeAction::Back,{}}},{PrototypeTrigger::Hover,Interaction{PrototypeAction::Navigate,target.id}}};REQUIRE(f.editor.setProps(source.id,p));
    REQUIRE_FALSE(f.session.edit(DeleteNode{target.id}));REQUIRE_FALSE(f.session.edit(MoveNode{target.id,container.id,"a0"}));NodePatch promote;promote.type=NodeType::Component;REQUIRE_FALSE(f.session.edit(SetProps{target.id,promote}));
    p.interactions->at(1).interaction.target={146,999};REQUIRE_FALSE(f.editor.setProps(source.id,p));p.interactions=std::vector<TriggeredInteraction>{};REQUIRE(f.editor.setProps(source.id,p));REQUIRE(f.session.edit(DeleteNode{target.id}));
}

TEST_CASE("Ordered component overrides replace inherited legacy actions and explicit empty lists persist") {
    Prototypes f;const auto target=f.create(),main=f.create(NodeType::Component),child=f.create(NodeType::Rect,main.id);NodePatch legacy;legacy.interaction=Interaction{PrototypeAction::Navigate,target.id};REQUIRE(f.editor.setProps(child.id,legacy));const auto instance=*f.editor.createInstance(main.id);const auto copy=f.session.document().viewChildren(instance).front();
    NodePatch p;p.interactions=std::vector<TriggeredInteraction>{{PrototypeTrigger::Press,Interaction{PrototypeAction::Back,{}}}};REQUIRE(f.session.edit(SetOverride{instance,child.id,p}));REQUIRE(f.session.document().view(copy)->interaction==Interaction{});REQUIRE(f.session.document().view(copy)->interactions.size()==1);
    p.interactions=std::vector<TriggeredInteraction>{};REQUIRE(f.session.edit(SetOverride{instance,child.id,p}));REQUIRE(f.session.document().view(copy)->interactions.empty());REQUIRE(f.session.document().view(copy)->interaction==Interaction{});
    const auto snapshot=snapshotToJson(f.session.document());REQUIRE(snapshot.find("\"interactions\":[]")!=std::string::npos);const auto ops=snapshotFromJson(snapshot);REQUIRE(ops.has_value());Document loaded;for(const auto& op:*ops) REQUIRE(loaded.apply(op));REQUIRE(loaded.view(copy)->interaction==Interaction{});
    REQUIRE_FALSE(f.session.edit(SetProps{instance,p}));ComponentDefinition definition;definition.axes.push_back({"state",{"a","b"},"a"});definition.rules={{{{"state","b"}},child.id,p}};REQUIRE_FALSE(f.editor.setComponentDefinition(main.id,definition));
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
TEST_CASE("Prototype overflow JSON patches are strict and preserve explicit clears") {
    for(const auto value:{"none","vertical","horizontal","both"}) {
        const auto patch=patchFromJson(std::string{"{\"prototypeOverflow\":\""}+value+"\"}");REQUIRE(patch);REQUIRE(patch->prototypeOverflow);
        const auto encoded=opToJson(SetProps{Id{146,1},*patch});REQUIRE(encoded.find(std::string{"\"prototypeOverflow\":\""}+value+"\"")!=std::string::npos);
        const auto decoded=opFromJson("SetProps",encoded);REQUIRE(decoded);REQUIRE(std::get<SetProps>(*decoded).patch==*patch);
    }
    for(const auto json:{R"({"prototypeOverflow":null})",R"({"prototypeOverflow":1})",R"({"prototypeOverflow":true})",R"({"prototypeOverflow":"auto"})",R"({"prototypeOverflow":"Vertical"})"}) REQUIRE_FALSE(patchFromJson(json));
    const auto invalid=opFromJson("CreateNode",R"({"id":"00000000-0000-0000-0000-000000000001","type":"rect","order":"a0","prototypeOverflow":"none"})");REQUIRE_FALSE(invalid);
}
TEST_CASE("Prototype overflow patch inverse merge undo and clear retain independent screen behavior") {
    Prototypes f;const auto screen=f.create();NodePatch overflow;overflow.prototypeOverflow=PrototypeOverflow::Vertical;NodePatch start;start.prototypeStart=true;
    const auto combined=mergePatch(overflow,start);REQUIRE(combined.prototypeOverflow==overflow.prototypeOverflow);REQUIRE(combined.prototypeStart==start.prototypeStart);
    Node node=screen;const auto inverse=applyPatch(node,combined);REQUIRE(node.prototypeOverflow==PrototypeOverflow::Vertical);applyPatch(node,inverse);REQUIRE(node==screen);
    REQUIRE(f.editor.setProps(screen.id,combined));REQUIRE(f.session.document().find(screen.id)->prototypeOverflow==PrototypeOverflow::Vertical);
    REQUIRE(f.editor.undo());REQUIRE(f.session.document().find(screen.id)->prototypeOverflow==PrototypeOverflow::None);REQUIRE(f.editor.redo());
    NodePatch clear;clear.prototypeOverflow=PrototypeOverflow::None;REQUIRE(f.editor.setProps(screen.id,clear));REQUIRE(f.session.document().find(screen.id)->prototypeStart);REQUIRE(f.editor.undo());REQUIRE(f.session.document().find(screen.id)->prototypeOverflow==PrototypeOverflow::Vertical);
    REQUIRE(mergePatch(combined,clear).prototypeOverflow==PrototypeOverflow::None);
}
TEST_CASE("Prototype overflow is restricted to authored root frames and cannot survive nesting or promotion") {
    Prototypes f;const auto root=f.create(),other=f.create(),child=f.create(NodeType::Frame,root.id),rect=f.create(NodeType::Rect),main=f.create(NodeType::Component);const auto instance=*f.editor.createInstance(main.id);
    for(const auto id:{child.id,rect.id,main.id,instance}) for(const auto value:{PrototypeOverflow::None,PrototypeOverflow::Vertical}) {NodePatch patch;patch.prototypeOverflow=value;REQUIRE_FALSE(f.session.edit(SetProps{id,patch}));}
    auto invalid=f.node(NodeType::Rect);invalid.prototypeOverflow=PrototypeOverflow::Both;REQUIRE_FALSE(f.session.edit(CreateNode{invalid}));invalid=f.node();invalid.prototypeOverflow=static_cast<PrototypeOverflow>(99);REQUIRE_FALSE(f.session.edit(CreateNode{invalid}));
    NodePatch patch;patch.prototypeOverflow=static_cast<PrototypeOverflow>(-1);REQUIRE_FALSE(f.session.edit(SetProps{root.id,patch}));patch.prototypeOverflow=PrototypeOverflow::Horizontal;REQUIRE(f.session.edit(SetProps{root.id,patch}));
    const auto before=snapshotToJson(f.session.document());REQUIRE_FALSE(f.session.edit(MoveNode{root.id,other.id,"a1"}));NodePatch promotion;promotion.type=NodeType::Component;REQUIRE_FALSE(f.session.edit(SetProps{root.id,promotion}));REQUIRE(snapshotToJson(f.session.document())==before);
    patch.prototypeOverflow=PrototypeOverflow::None;REQUIRE(f.session.edit(SetProps{root.id,patch}));REQUIRE(f.session.edit(SetProps{root.id,promotion}));
}
TEST_CASE("Prototype overflow forbids component overrides and variant rules including explicit none") {
    Prototypes f;const auto main=f.create(NodeType::Component),child=f.create(NodeType::Frame,main.id);const auto instance=*f.editor.createInstance(main.id);
    for(const auto value:{PrototypeOverflow::None,PrototypeOverflow::Both}) {
        NodePatch patch;patch.prototypeOverflow=value;REQUIRE_FALSE(f.session.edit(SetOverride{instance,main.id,patch}));REQUIRE_FALSE(f.session.edit(SetOverride{instance,child.id,patch}));
        auto raw=f.node(NodeType::Instance);raw.component=main.id;raw.overrides={{child.id,patch}};REQUIRE_FALSE(f.session.edit(CreateNode{raw}));
        ComponentDefinition definition;definition.axes.push_back({"state",{"a","b"},"a"});definition.rules={{{{"state","b"}},child.id,patch}};REQUIRE_FALSE(f.editor.setComponentDefinition(main.id,definition));
    }
}
TEST_CASE("Prototype overflow snapshot19 preserves each axis refuses downgrades and keeps default snapshots minimal") {
    for(const auto value:{PrototypeOverflow::Vertical,PrototypeOverflow::Horizontal,PrototypeOverflow::Both}) {
        Prototypes f;const auto screen=f.create();NodePatch patch;patch.prototypeOverflow=value;REQUIRE(f.editor.setProps(screen.id,patch));const auto snapshot=snapshotToJson(f.session.document());REQUIRE(snapshot.find("\"version\":19")!=std::string::npos);
        const auto ops=snapshotFromJson(snapshot);REQUIRE(ops.has_value());Document loaded;for(const auto& op:*ops)REQUIRE(loaded.apply(op));REQUIRE(loaded.find(screen.id)->prototypeOverflow==value);
        auto old=snapshot;old.replace(old.find("\"version\":19"),12,"\"version\":18");REQUIRE_FALSE(snapshotFromJson(old).has_value());
        patch.prototypeOverflow=PrototypeOverflow::None;REQUIRE(f.editor.setProps(screen.id,patch));const auto plain=snapshotToJson(f.session.document());REQUIRE(plain.find("prototypeOverflow")==std::string::npos);REQUIRE(plain.find("\"version\":2")!=std::string::npos);
    }
    REQUIRE_FALSE(snapshotFromJson(R"({"version":18,"modes":[],"tokens":[],"nodes":[{"id":"00000000-0000-0000-0000-000000000001","type":"frame","order":"a0","prototypeOverflow":"none"}]})").has_value());
}
