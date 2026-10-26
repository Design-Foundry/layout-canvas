#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <limits>
#include "vc/draw_list.hpp"
#include "vc/geometry.hpp"

using Catch::Matchers::WithinAbs;
using vc::CreateNode;
using vc::Document;
using vc::DrawItem;
using vc::DrawKind;
using vc::Id;
using vc::Node;
using vc::NodeType;

TEST_CASE("Moving presentation layers stay clipped to their stationary viewport", "[draw][prototype]") {
    DrawItem item;item.kind=DrawKind::Image;item.quad={10,20,30,40};
    item.clip={0,0,100,80};item.a.rect={.1,.2,.3,.4};item.color={1,1,1,1};
    vc::transformDrawItem(item,{50,0},1,vc::Rect{0,0,100,80});
    REQUIRE(item.clip.x==50);REQUIRE(item.clip.w==50);
    REQUIRE(item.quad.x==60);REQUIRE_THAT(item.a.rect.x,WithinAbs(.1,.00001));
    vc::transformDrawItem(item,{200,0},1,vc::Rect{0,0,100,80});
    REQUIRE(item.clip.w==0);
}

TEST_CASE("Presentation translations preserve texture UVs and move paths and clipping", "[draw][prototype]") {
    for(const auto kind:{DrawKind::Glyph,DrawKind::Image,DrawKind::Path,DrawKind::Segment}) {
        DrawItem item;item.kind=kind;item.quad={10,20,30,40};item.clip={1,2,90,80};
        item.a.rect={.1,.2,.3,.4};item.b.rect={8,9,0,0};item.points={{4,5},{6,7}};
        item.color={1,1,1,.8f};vc::transformDrawItem(item,{100,200},.5);
        REQUIRE(item.quad.x==110);REQUIRE(item.quad.y==220);
        REQUIRE(item.clip.x==101);REQUIRE(item.clip.y==202);
        REQUIRE(item.b.rect.x==108);REQUIRE(item.points[0].x==104);
        REQUIRE_THAT(item.color.a,WithinAbs(.4,.00001));
        REQUIRE_THAT(item.a.rect.x,WithinAbs(kind==DrawKind::Glyph || kind==DrawKind::Image?.1:100.1,.00001));
    }
}
using vc::Rect;

namespace {

Id idOf(std::uint64_t n) {
    return {4, n};
}

Node rect(std::uint64_t id, double x, double y, double w, double h, std::uint64_t parent = 0) {
    Node n;
    n.id = idOf(id);
    n.parent = parent ? idOf(parent) : Id{};
    n.order = "a" + std::to_string(id % 10);
    n.x = x;
    n.y = y;
    n.w = w;
    n.h = h;
    return n;
}

const Rect kView{-1000, -1000, 4000, 4000};

std::vector<DrawItem> draw(const Document& doc, const Rect& view = kView) {
    std::vector<DrawItem> items;
    vc::buildDrawList(doc, view, vc::kDefaultModeId, items);
    return items;
}

bool same(const Rect& a, const Rect& b) {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

} // namespace

TEST_CASE("Prototype scrolling moves descendants while keeping the viewport and root paint fixed", "[draw][prototype][scroll]") {
    Document doc;
    auto frame=rect(1,100,200,100,80);frame.type=NodeType::Frame;frame.clipContent=true;
    REQUIRE(doc.apply(CreateNode{frame}));
    REQUIRE(doc.apply(CreateNode{rect(2,10,110,30,20,1)}));
    std::vector<DrawItem> items;
    vc::buildDrawList(doc,kView,vc::kDefaultModeId,items,frame.id,{0,100});
    REQUIRE(items.size()==2);
    REQUIRE(same(items[0].quad,{100,200,100,80}));
    REQUIRE(same(items[1].quad,{110,210,30,20}));
    REQUIRE(same(items[1].clip,{100,200,100,80}));
    REQUIRE(vc::hitTest(doc,{120,220},false,vc::kDefaultModeId,0,frame.id,true,{0,100})==idOf(2));
    REQUIRE(vc::hitTest(doc,{120,220},false,vc::kDefaultModeId,0,frame.id,true)==frame.id);
    REQUIRE(vc::hitTest(doc,{120,290},false,vc::kDefaultModeId,0,frame.id,true,{0,100}).isNil());
    // Scrolling is scoped to presentation: the ordinary editor geometry stays authored.
    REQUIRE(vc::hitTest(doc,{120,320}).isNil());
    REQUIRE(doc.box(idOf(2)).y==110);
}

TEST_CASE("Scrolled nested clipping agrees between native paint and picking", "[draw][prototype][scroll]") {
    Document doc;
    auto frame=rect(1,100,200,100,80);frame.type=NodeType::Frame;frame.clipContent=false;
    auto nested=rect(2,10,100,40,30,1);nested.type=NodeType::Frame;nested.clipContent=true;
    REQUIRE(doc.apply(CreateNode{frame}));REQUIRE(doc.apply(CreateNode{nested}));
    REQUIRE(doc.apply(CreateNode{rect(3,0,20,20,30,2)}));
    std::vector<DrawItem> items;
    vc::buildDrawList(doc,kView,vc::kDefaultModeId,items,frame.id,{0,90});
    REQUIRE(items.size()==3);
    REQUIRE(same(items[2].quad,{110,230,20,30}));
    REQUIRE(same(items[2].clip,{110,210,40,30}));
    REQUIRE(vc::hitTest(doc,{115,235},false,vc::kDefaultModeId,0,frame.id,true,{0,90})==idOf(3));
    REQUIRE(vc::hitTest(doc,{115,250},false,vc::kDefaultModeId,0,frame.id,true,{0,90})==frame.id);
}

TEST_CASE("Prototype scroll range excludes hidden and clipped overflow but includes expanded copies", "[geometry][prototype][scroll]") {
    Document doc;
    auto frame=rect(1,100,200,100,80);frame.type=NodeType::Frame;frame.clipContent=true;
    auto nested=rect(2,0,70,40,20,1);nested.type=NodeType::Frame;nested.clipContent=true;
    REQUIRE(doc.apply(CreateNode{frame}));REQUIRE(doc.apply(CreateNode{nested}));
    REQUIRE(doc.apply(CreateNode{rect(3,0,200,20,20,2)}));
    auto hidden=rect(4,500,500,100,100,1);hidden.type=NodeType::Frame;hidden.visible=false;
    REQUIRE(doc.apply(CreateNode{hidden}));REQUIRE(doc.apply(CreateNode{rect(5,900,900,100,100,4)}));
    auto limit=vc::prototypeScrollLimit(doc,frame.id);
    REQUIRE(limit.x==0);REQUIRE(limit.y==10);
    auto main=rect(6,1000,1000,30,20);main.type=NodeType::Component;
    REQUIRE(doc.apply(CreateNode{main}));REQUIRE(doc.apply(CreateNode{rect(7,50,100,20,20,6)}));
    auto instance=rect(8,80,60,30,20,1);instance.type=NodeType::Instance;instance.component=main.id;
    REQUIRE(doc.apply(CreateNode{instance}));
    limit=vc::prototypeScrollLimit(doc,frame.id);
    REQUIRE(limit.x==50);REQUIRE(limit.y==100);
    // The root's own clip does not truncate the content extent.
    REQUIRE(frame.clipContent);
}

TEST_CASE("paint order is drop shadows, fill, inner shadows, children, then stroke") {
    Document doc;
    Node frame = rect(1, 0, 0, 100, 100);
    frame.type = NodeType::Frame;
    vc::Stroke stroke;
    stroke.width = 2.0;
    frame.stroke = stroke;
    frame.shadows = std::vector<vc::Shadow>{{0, 4, 8, 0, vc::Oklch{0, 0, 0, 0.3}, false, true},
                                            {0, 1, 2, 0, vc::Oklch{0, 0, 0, 0.3}, true, true}};
    doc.apply(CreateNode{frame});
    doc.apply(CreateNode{rect(2, 10, 10, 20, 20, 1)});

    const auto items = draw(doc);
    REQUIRE(items.size() == 5);
    REQUIRE(items[0].kind == DrawKind::DropShadow);
    REQUIRE(items[1].kind == DrawKind::Fill);
    REQUIRE(items[2].kind == DrawKind::InnerShadow);
    REQUIRE(items[3].kind == DrawKind::Fill);
    REQUIRE(same(items[3].a.rect, {10, 10, 20, 20}));
    REQUIRE(items[4].kind == DrawKind::Stroke);
}

TEST_CASE("stroke alignment moves the stroke band relative to the box") {
    const auto band = [](vc::StrokeAlign align) {
        Document doc;
        Node n = rect(1, 0, 0, 100, 50);
        vc::Stroke stroke;
        stroke.width = 4.0;
        stroke.align = align;
        n.stroke = stroke;
        doc.apply(CreateNode{n});
        const auto items = draw(doc);
        return std::pair{items.back().a.rect, items.back().b.rect};
    };
    const auto [insideOuter, insideInner] = band(vc::StrokeAlign::Inside);
    REQUIRE(same(insideOuter, {0, 0, 100, 50}));
    REQUIRE(same(insideInner, {4, 4, 92, 42}));

    const auto [centerOuter, centerInner] = band(vc::StrokeAlign::Center);
    REQUIRE(same(centerOuter, {-2, -2, 104, 54}));
    REQUIRE(same(centerInner, {2, 2, 96, 46}));

    const auto [outsideOuter, outsideInner] = band(vc::StrokeAlign::Outside);
    REQUIRE(same(outsideOuter, {-4, -4, 108, 58}));
    REQUIRE(same(outsideInner, {0, 0, 100, 50}));
}

TEST_CASE("per-side stroke widths inset the inner box per side") {
    Document doc;
    Node n = rect(1, 0, 0, 100, 50);
    vc::Stroke stroke;
    stroke.width = 1.0;
    stroke.sides[vc::kBottom] = 3.0;
    n.stroke = stroke;
    doc.apply(CreateNode{n});

    REQUIRE(same(draw(doc).back().b.rect, {1, 1, 98, 46}));
}

TEST_CASE("drop shadows offset, spread and reach three sigmas") {
    Document doc;
    Node n = rect(1, 0, 0, 100, 50);
    n.cornerRadius = 8.0;
    n.shadows = std::vector<vc::Shadow>{{2, 4, 10, 3, vc::Oklch{0, 0, 0, 0.5}, false, true}};
    doc.apply(CreateNode{n});

    const DrawItem shadow = draw(doc).front();
    REQUIRE(same(shadow.a.rect, {-1, 1, 106, 56}));
    REQUIRE(shadow.a.radii[0] == 11);
    REQUIRE(shadow.sigma == 5);
    REQUIRE(same(shadow.quad, {-16, -14, 136, 86}));
}

TEST_CASE("hidden fills, hidden shadows and opacity") {
    Document doc;
    Node n = rect(1, 0, 0, 10, 10);
    n.fillVisible = false;
    n.shadows = std::vector<vc::Shadow>{{0, 0, 0, 0, vc::Oklch{0, 0, 0, 1}, false, false}};
    doc.apply(CreateNode{n});
    REQUIRE(draw(doc).empty());

    Node parent = rect(2, 0, 0, 50, 50);
    parent.type = NodeType::Frame;
    parent.opacity = 0.5;
    Node child = rect(3, 0, 0, 10, 10, 2);
    child.fill = vc::Oklch{0, 0, 0, 1};
    child.opacity = 0.5;
    doc.apply(CreateNode{parent});
    doc.apply(CreateNode{child});
    REQUIRE_THAT(draw(doc).back().color.a, WithinAbs(0.25, 1e-6));
}

TEST_CASE("nodes outside the view are skipped unless their shadow reaches in") {
    Document doc;
    doc.apply(CreateNode{rect(1, 500, 0, 10, 10)});
    Node shadowed = rect(2, 520, 0, 10, 10);
    shadowed.shadows = std::vector<vc::Shadow>{{-30, 0, 0, 0, vc::Oklch{0, 0, 0, 1}, false, true}};
    doc.apply(CreateNode{shadowed});

    const auto items = draw(doc, {0, 0, 495, 100});
    REQUIRE(items.size() == 2);
    REQUIRE(items[0].kind == DrawKind::DropShadow);
}

TEST_CASE("clipping frames pass their box to children") {
    Document doc;
    Node frame = rect(1, 10, 10, 50, 50);
    frame.type = NodeType::Frame;
    frame.clipContent = true;
    doc.apply(CreateNode{frame});
    doc.apply(CreateNode{rect(2, 40, 40, 30, 30, 1)});

    const auto items = draw(doc);
    REQUIRE(same(items.back().clip, {10, 10, 50, 50}));
    REQUIRE(same(items.back().a.rect, {50, 50, 30, 30}));
}
TEST_CASE("Presentation affine scaling preserves old viewport geometry and texture coordinates") {
    DrawItem item;item.kind=DrawKind::Image;item.quad={10,20,30,40};item.clip={0,0,100,80};
    item.a.rect={.1,.2,.3,.4};item.b.rect={8,9,0,0};item.points={{4,5},{6,7}};
    item.a.radii={1,2,3,4};item.strokeWidth=3;item.sigma=2;item.color={1,1,1,.8f};
    vc::transformDrawItem(item,{100,200},1,std::nullopt,2);
    REQUIRE(item.quad.x==120);REQUIRE(item.quad.w==60);REQUIRE(item.clip.w==200);
    REQUIRE(item.points[0].x==108);REQUIRE(item.a.radii[3]==8);REQUIRE(item.strokeWidth==6);REQUIRE(item.sigma==4);
    REQUIRE_THAT(item.a.rect.x,WithinAbs(.1,.00001));REQUIRE_THAT(item.color.a,WithinAbs(.8,.00001));
}

TEST_CASE("Smart Animate identities retain parent paints around children and distinguish instance copies") {
    Document doc;Node parent=rect(1,0,0,100,80);parent.type=NodeType::Frame;
    vc::Stroke stroke;stroke.width=2.;parent.stroke=stroke;
    REQUIRE(doc.apply(CreateNode{parent}).has_value());REQUIRE(doc.apply(CreateNode{rect(2,10,10,20,20,1)}).has_value());
    const auto paints=draw(doc);REQUIRE(paints.size()==3);
    REQUIRE(paints[0].node==parent.id);REQUIRE(paints[0].primitive==0);
    REQUIRE(paints[1].node==idOf(2));REQUIRE(paints[1].primitive==0);
    REQUIRE(paints[2].node==parent.id);REQUIRE(paints[2].primitive==1);

    Node component=rect(3,200,0,50,40);component.type=NodeType::Component;
    REQUIRE(doc.apply(CreateNode{component}).has_value());REQUIRE(doc.apply(CreateNode{rect(4,5,5,10,10,3)}).has_value());
    for(std::uint64_t id:{5,6}) {Node instance=rect(id,300+double(id)*50,0,50,40);instance.type=NodeType::Instance;instance.component=component.id;REQUIRE(doc.apply(CreateNode{instance}).has_value());}
    const auto copyA=doc.viewChildren(idOf(5)).front(),copyB=doc.viewChildren(idOf(6)).front();REQUIRE(copyA!=copyB);
    const auto all=draw(doc);bool foundA=false,foundB=false;
    for(const auto& paint:all) {foundA=foundA || paint.node==copyA;foundB=foundB || paint.node==copyB;}
    REQUIRE(foundA);REQUIRE(foundB);REQUIRE(vc::interpolateDrawLists(all,draw(doc),.5).has_value());
}

TEST_CASE("Smart Animate interpolates native paint geometry color radius clip and exact endpoints") {
    DrawItem a;a.node=idOf(1);a.quad={0,10,20,30};a.a.rect=a.quad;a.b.rect={2,12,16,26};a.clip={-10,-20,100,200};
    a.a.radii={0,2,4,6};a.b.radii={1,3,5,7};a.color={.2f,.4f,.6f,.8f};a.sigma=2;a.strokeWidth=4;
    auto b=a;b.quad={100,50,60,70};b.a.rect=b.quad;b.b.rect={110,60,40,50};b.clip={10,20,200,300};
    b.a.radii={10,12,14,16};b.b.radii={11,13,15,17};b.color={.8f,.6f,.4f,.2f};b.sigma=8;b.strokeWidth=10;
    const auto result=vc::interpolateDrawLists({a},{b},.5);REQUIRE(result.has_value());const auto& middle=result->front();
    REQUIRE(same(middle.quad,{50,30,40,50}));REQUIRE(same(middle.a.rect,middle.quad));REQUIRE(same(middle.b.rect,{56,36,28,38}));
    REQUIRE(same(middle.clip,{0,0,150,250}));REQUIRE(middle.a.radii[0]==5);REQUIRE(middle.b.radii[3]==12);
    REQUIRE(middle.sigma==5);REQUIRE(middle.strokeWidth==7);REQUIRE_THAT(middle.color.r,WithinAbs(.5,.00001));REQUIRE_THAT(middle.color.a,WithinAbs(.5,.00001));
    const auto start=vc::interpolateDrawLists({a},{b},0),end=vc::interpolateDrawLists({a},{b},1);REQUIRE(start.has_value());REQUIRE(end.has_value());
    REQUIRE(same(start->front().quad,a.quad));REQUIRE(start->front().color.r==a.color.r);REQUIRE(same(end->front().quad,b.quad));REQUIRE(end->front().color.a==b.color.a);
}

TEST_CASE("Smart Animate refuses missing reordered ambiguous or semantically different paints") {
    DrawItem a;a.node=idOf(1);auto b=a;b.node=idOf(2);
    REQUIRE_FALSE(vc::interpolateDrawLists({a},{a,b},.5).has_value());
    REQUIRE_FALSE(vc::interpolateDrawLists({a,b},{b,a},.5).has_value());
    REQUIRE_FALSE(vc::interpolateDrawLists({a,a},{a,a},.5).has_value());
    auto nil=a;nil.node={};REQUIRE_FALSE(vc::interpolateDrawLists({nil},{nil},.5).has_value());
    auto changed=a;changed.kind=DrawKind::Stroke;REQUIRE_FALSE(vc::interpolateDrawLists({a},{changed},.5).has_value());
    changed=a;changed.ellipse=true;REQUIRE_FALSE(vc::interpolateDrawLists({a},{changed},.5).has_value());
    changed=a;changed.style=vc::StrokeStyle::Dashed;REQUIRE_FALSE(vc::interpolateDrawLists({a},{changed},.5).has_value());
    REQUIRE_FALSE(vc::interpolateDrawLists({a},{a},std::numeric_limits<double>::quiet_NaN()).has_value());
    REQUIRE_FALSE(vc::interpolateDrawLists({a},{a},std::numeric_limits<double>::infinity()).has_value());
}

TEST_CASE("Smart Animate preserves glyph and image texture coordinates and refuses texture changes") {
    for(const auto kind:{DrawKind::Glyph,DrawKind::Image}) {
        vc::Raster raster{2,2,"AAAAAA==","same-asset"};
        DrawItem a;a.node=idOf(1);a.kind=kind;a.a.rect={.1,.2,.3,.4};a.quad={0,0,20,20};
        if(kind==DrawKind::Image) a.image=&raster;
        auto b=a;b.quad={30,40,60,80};
        const auto result=vc::interpolateDrawLists({a},{b},.5);REQUIRE(result.has_value());
        REQUIRE(same(result->front().a.rect,a.a.rect));REQUIRE(same(result->front().quad,{15,20,40,50}));
        b.a.rect.x=.2;REQUIRE_FALSE(vc::interpolateDrawLists({a},{b},.5).has_value());
        if(kind==DrawKind::Image) {
            b=a;auto other=raster;other.id="different-asset";b.image=&other;REQUIRE_FALSE(vc::interpolateDrawLists({a},{b},.5).has_value());
            other=raster;other.width=3;REQUIRE_FALSE(vc::interpolateDrawLists({a},{b},.5).has_value());
        }
    }
}

TEST_CASE("Smart Animate interpolates compatible path vertices and refuses changed topology") {
    DrawItem a;a.node=idOf(1);a.kind=DrawKind::Path;a.points={{0,0},{10,20},{20,0}};a.closed=true;a.filled=true;
    auto b=a;b.points={{10,20},{20,40},{30,20}};
    const auto result=vc::interpolateDrawLists({a},{b},.25);REQUIRE(result.has_value());REQUIRE(result->front().points[1].x==12.5);REQUIRE(result->front().points[1].y==25);
    b.points.pop_back();REQUIRE_FALSE(vc::interpolateDrawLists({a},{b},.5).has_value());
    b=a;b.closed=false;REQUIRE_FALSE(vc::interpolateDrawLists({a},{b},.5).has_value());
    b=a;b.filled=false;REQUIRE_FALSE(vc::interpolateDrawLists({a},{b},.5).has_value());
}

TEST_CASE("Smart Animate glyph identities retain original slots when clipping hides repeated glyphs") {
    Document doc;Node text=rect(1,0,0,300,40);text.type=NodeType::Text;text.text.content="AAAAAA";
    REQUIRE(doc.apply(CreateNode{text}).has_value());
    const auto full=draw(doc);REQUIRE(full.size()>=4);
    const auto clipped=draw(doc,{full[2].quad.x,0,300,40});REQUIRE_FALSE(clipped.empty());REQUIRE(clipped.front().primitive>=1);
    for(const auto& paint:clipped) {
        REQUIRE(paint.node==text.id);REQUIRE(paint.primitive<full.size());REQUIRE(same(paint.quad,full[paint.primitive].quad));
    }
    REQUIRE_FALSE(vc::interpolateDrawLists(full,clipped,.5).has_value());
}

namespace {
void namedScreen(Document& doc,std::uint64_t base,double x=0) {
    auto screen=rect(base,x,0,300,300);screen.type=NodeType::Frame;screen.name="Screen "+std::to_string(base);
    auto group=rect(base+1,10,20,200,180,base);group.type=NodeType::Frame;group.name="Card/primary";
    auto button=rect(base+2,10,15,50,30,base+1);button.name="Continue";
    REQUIRE(doc.apply(CreateNode{screen}).has_value());REQUIRE(doc.apply(CreateNode{group}).has_value());
    REQUIRE(doc.apply(CreateNode{button}).has_value());
}
std::vector<DrawItem> screenPaints(const Document& doc,std::uint64_t root) {
    std::vector<DrawItem> items;vc::buildDrawList(doc,kView,vc::kDefaultModeId,items,idOf(root));return items;
}
}

TEST_CASE("Cross-screen Smart Animate matches structured names while ignoring root names and geometry") {
    Document source,target;namedScreen(source,1);namedScreen(target,11,500);
    vc::NodePatch patch;patch.x=80.;patch.w=100.;patch.fill=vc::Oklch{.5,.1,120};
    REQUIRE(target.apply(vc::SetProps{idOf(13),patch}).has_value());
    const auto from=screenPaints(source,1),to=screenPaints(target,11);
    REQUIRE_FALSE(vc::interpolateDrawLists(from,to,.5).has_value());
    const auto remapped=vc::remapDrawListIdentities(source,idOf(1),target,idOf(11),to);
    REQUIRE(remapped.has_value());REQUIRE(remapped->size()==to.size());
    REQUIRE(remapped->back().node==idOf(3));REQUIRE(to.back().node==idOf(13));
    const auto animated=vc::interpolateDrawLists(from,*remapped,.5);REQUIRE(animated.has_value());
    REQUIRE_THAT(animated->back().quad.x,WithinAbs((from.back().quad.x+to.back().quad.x)/2,.00001));
    REQUIRE_THAT(animated->back().quad.w,WithinAbs(75.,.00001));
    REQUIRE(source.find(idOf(3))->name=="Continue");REQUIRE(target.find(idOf(13))->name=="Continue");
}

TEST_CASE("Cross-screen matching refuses unnamed ambiguous renamed missing or differently typed hierarchy") {
    for(const auto problem:{"empty","duplicate","rename","type","missing","source-duplicate"}) {
        Document source,target;namedScreen(source,1);namedScreen(target,11);
        if(std::string(problem)=="missing") REQUIRE(target.apply(vc::DeleteNode{idOf(13)}).has_value());
        else if(std::string(problem)=="type") {
            // Type conversion is intentionally restricted by the editing model;
            // construct a genuinely different destination layer instead.
            auto replacement=*target.find(idOf(13));replacement.type=NodeType::Ellipse;
            REQUIRE(target.apply(vc::DeleteNode{idOf(13)}).has_value());
            REQUIRE(target.apply(CreateNode{replacement}).has_value());
        }
        else if(std::string(problem)=="duplicate" || std::string(problem)=="source-duplicate") {
            const bool before=std::string(problem)=="source-duplicate";
            auto duplicate=rect(before?4:14,0,0,10,10,before?2:12);duplicate.name="Continue";
            REQUIRE((before?source:target).apply(CreateNode{duplicate}).has_value());
        } else {
            vc::NodePatch patch;
            patch.name=std::string(problem)=="empty"?"":"Changed";
            REQUIRE(target.apply(vc::SetProps{idOf(13),patch}).has_value());
        }
        REQUIRE_FALSE(vc::remapDrawListIdentities(source,idOf(1),target,idOf(11),screenPaints(target,11)).has_value());
    }
    Document source,target;namedScreen(source,1);namedScreen(target,11);
    // Even nonpainting descendants are part of the identity contract.
    auto hidden=rect(14,0,0,10,10,12);hidden.name="";hidden.visible=false;
    REQUIRE(target.apply(CreateNode{hidden}).has_value());
    REQUIRE_FALSE(vc::remapDrawListIdentities(source,idOf(1),target,idOf(11),screenPaints(target,11)).has_value());
    REQUIRE_FALSE(vc::remapDrawListIdentities(source,idOf(2),target,idOf(12),{}).has_value());
}

TEST_CASE("Cross-screen remapping retains paint order texture asset and topology safeguards") {
    Document source,target;namedScreen(source,1);namedScreen(target,11);
    const auto from=screenPaints(source,1),to=screenPaints(target,11);
    auto remapped=vc::remapDrawListIdentities(source,idOf(1),target,idOf(11),to);REQUIRE(remapped.has_value());
    auto reordered=*remapped;std::swap(reordered[0],reordered[1]);
    REQUIRE_FALSE(vc::interpolateDrawLists(from,reordered,.5).has_value());
    for(const auto kind:{DrawKind::Glyph,DrawKind::Image,DrawKind::Path}) {
        auto a=from,b=to;a.back().kind=kind;b.back().kind=kind;
        vc::Raster asset{2,2,"AAAAAA==","same"},other=asset;
        if(kind==DrawKind::Image) {a.back().image=&asset;b.back().image=&asset;}
        auto matched=vc::remapDrawListIdentities(source,idOf(1),target,idOf(11),b);REQUIRE(matched.has_value());
        REQUIRE(vc::interpolateDrawLists(a,*matched,.5).has_value());
        if(kind==DrawKind::Path) b.back().points={{0,0}};
        else b.back().a.rect.x+=.1;
        matched=vc::remapDrawListIdentities(source,idOf(1),target,idOf(11),b);REQUIRE(matched.has_value());
        REQUIRE_FALSE(vc::interpolateDrawLists(a,*matched,.5).has_value());
        if(kind==DrawKind::Image) {
            b=a;b.back().node=idOf(13);b[0].node=idOf(11);b[1].node=idOf(12);other.id="new";b.back().image=&other;
            matched=vc::remapDrawListIdentities(source,idOf(1),target,idOf(11),b);REQUIRE(matched.has_value());
            REQUIRE_FALSE(vc::interpolateDrawLists(a,*matched,.5).has_value());
        }
    }
    auto outside=to;outside.back().node=idOf(99);
    REQUIRE_FALSE(vc::remapDrawListIdentities(source,idOf(1),target,idOf(11),outside).has_value());
}

TEST_CASE("Cross-screen hierarchy resolves repeated child names in independent expanded instance paths") {
    Document source,target;
    const auto build=[](Document& doc,std::uint64_t base) {
        auto root=rect(base,0,0,300,300);root.type=NodeType::Frame;
        auto main=rect(base+1,500,0,60,60);main.type=NodeType::Component;main.name="Master";
        auto child=rect(base+2,0,0,20,20,base+1);child.name="Icon";
        REQUIRE(doc.apply(CreateNode{root}).has_value());REQUIRE(doc.apply(CreateNode{main}).has_value());
        REQUIRE(doc.apply(CreateNode{child}).has_value());
        for(std::uint64_t i=0;i<2;++i) {
            auto instance=rect(base+3+i,double(i)*90,0,60,60,base);instance.type=NodeType::Instance;
            instance.component=main.id;instance.name=i?"Secondary":"Primary";
            REQUIRE(doc.apply(CreateNode{instance}).has_value());
        }
    };
    build(source,1);build(target,11);
    const auto from=screenPaints(source,1),to=screenPaints(target,11);
    const auto matched=vc::remapDrawListIdentities(source,idOf(1),target,idOf(11),to);REQUIRE(matched.has_value());
    REQUIRE(vc::interpolateDrawLists(from,*matched,.5).has_value());
    const auto first=source.viewChildren(idOf(4)).front(),second=source.viewChildren(idOf(5)).front();
    REQUIRE(first!=second);
    REQUIRE(std::any_of(matched->begin(),matched->end(),[&](const auto& item){return item.node==first;}));
    REQUIRE(std::any_of(matched->begin(),matched->end(),[&](const auto& item){return item.node==second;}));
}
