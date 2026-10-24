#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "vc/editor.hpp"
#include "vc/draw_list.hpp"
#include "vc/geometry.hpp"
#include "vc/json.hpp"
#include "vc/raster.hpp"
using namespace vc;
using Catch::Matchers::WithinAbs;
namespace {
Raster raster(int w=2,int h=1,std::uint8_t seed=255) {
    std::vector<std::uint8_t> pixels(std::size_t(w)*h*4,seed);
    return {w,h,encodeRasterPixels(pixels),rasterDigest(w,h,pixels)};
}
struct Images {
    Session session{IdGenerator{132}};Camera camera;Editor editor{session,camera};
    Node node(){Node n;n.id=session.newId();n.type=NodeType::Image;n.order="a0";n.x=20;n.y=30;n.w=100;n.h=100;n.image=raster();return n;}
};
}
TEST_CASE("Immutable raster hashing matches independent SHA256 vector and canonical RGBA base64") {
    const std::vector<std::uint8_t> bytes{255,0,0,128};
    Raster image{1,1,"/wAAgA==","c1034074bbdc364cba7be6af4f2cccd5b4083577bb4ca69ceb571385757a8405"};
    REQUIRE(rasterDigest(1,1,bytes)==image.id);REQUIRE(encodeRasterPixels(bytes)==image.pixels);REQUIRE(validRaster(image));
    const auto decoded=decodeRaster(image);REQUIRE(decoded.has_value());REQUIRE(*decoded==bytes);
    image.pixels="/wAAgB==";REQUIRE_FALSE(validRaster(image));image.pixels="/wAAgA==";image.id[0]='a';REQUIRE_FALSE(validRaster(image));
    image=raster();image.width=513;REQUIRE_FALSE(validRaster(image));image=raster();image.pixels.pop_back();REQUIRE_FALSE(validRaster(image));
}
TEST_CASE("Image fit modes produce native contain bounds and centered crop UVs without stretching cover") {
    Images f;auto n=f.node();REQUIRE(f.session.edit(CreateNode{n}));std::vector<DrawItem> items;
    REQUIRE(buildDrawList(f.session.document(),{0,0,300,300},kDefaultModeId,items)==1);REQUIRE(items.size()==1);REQUIRE(items[0].kind==DrawKind::Image);
    REQUIRE(items[0].quad.w==100);REQUIRE(items[0].a.rect.x==.25);REQUIRE(items[0].a.rect.w==.5);REQUIRE(items[0].image->id==n.image.id);
    NodePatch p;p.imageFit="contain";REQUIRE(f.editor.setProps(n.id,p));REQUIRE(buildDrawList(f.session.document(),{0,0,300,300},kDefaultModeId,items)==1);
    REQUIRE(items[0].quad.x==20);REQUIRE(items[0].quad.y==55);REQUIRE(items[0].quad.w==100);REQUIRE(items[0].quad.h==50);REQUIRE(items[0].a.rect.w==1);
    p.imageFit="stretch";p.imageCrop=ImageCrop{.5,0,.5,1};p.opacity=.25;REQUIRE(f.editor.setProps(n.id,p));REQUIRE(buildDrawList(f.session.document(),{0,0,300,300},kDefaultModeId,items)==1);
    REQUIRE(items[0].quad.h==100);REQUIRE(items[0].a.rect.x==.5);REQUIRE(items[0].a.rect.w==.5);REQUIRE_THAT(items[0].color.a,WithinAbs(.25,1e-6));
    p.fillVisible=false;REQUIRE(f.editor.setProps(n.id,p));REQUIRE(buildDrawList(f.session.document(),{0,0,300,300},kDefaultModeId,items)==0);
}
TEST_CASE("Native images retain clipping and generic box picking under frames") {
    Images f;Node frame;frame.id=f.session.newId();frame.type=NodeType::Frame;frame.order="a0";frame.w=70;frame.h=80;frame.clipContent=true;REQUIRE(f.session.edit(CreateNode{frame}));
    auto n=f.node();n.parent=frame.id;REQUIRE(f.session.edit(CreateNode{n}));REQUIRE(hitTest(f.session.document(),{30,40})==n.id);REQUIRE(hitTest(f.session.document(),{100,100}).isNil());
    std::vector<DrawItem> items;REQUIRE(buildDrawList(f.session.document(),{0,0,300,300},kDefaultModeId,items)==2);const auto& image=items.back();REQUIRE(image.kind==DrawKind::Image);REQUIRE(image.clip.w==50);REQUIRE(image.clip.h==50);
}
TEST_CASE("Image fit and crop overrides inherit independent asset replacements and persist only in snapshot15") {
    Images f;Node main;main.id=f.session.newId();main.type=NodeType::Component;main.order="a0";main.w=200;main.h=200;REQUIRE(f.session.edit(CreateNode{main}));
    auto n=f.node();n.parent=main.id;REQUIRE(f.session.edit(CreateNode{n}));const auto instance=*f.editor.createInstance(main.id);const auto copy=f.session.document().viewChildren(instance).front();
    NodePatch local;local.imageFit="contain";local.imageCrop=ImageCrop{0,0,.5,1};REQUIRE(f.editor.setProps(copy,local));NodePatch source;source.image=raster(1,2,128);REQUIRE(f.editor.setProps(n.id,source));
    REQUIRE(f.session.document().view(copy)->image.id==source.image->id);REQUIRE(f.session.document().view(copy)->imageFit=="contain");REQUIRE(f.session.document().view(copy)->imageCrop.w==.5);
    const auto json=snapshotToJson(f.session.document());REQUIRE(json.find("\"version\":15")!=std::string::npos);const auto ops=snapshotFromJson(json);REQUIRE(ops.has_value());Document loaded;for(const auto& op:*ops)REQUIRE(loaded.apply(op));REQUIRE(loaded.view(copy)->image==*source.image);REQUIRE(loaded.view(copy)->imageFit=="contain");
    auto old=json;old.replace(old.find("\"version\":15"),12,"\"version\":14");REQUIRE_FALSE(snapshotFromJson(old).has_value());
    REQUIRE(f.editor.resetOverrides(copy));REQUIRE(f.session.document().view(copy)->imageFit=="cover");REQUIRE(f.editor.undo());REQUIRE(f.session.document().view(copy)->imageFit=="contain");
    ComponentDefinition def;def.axes.push_back({"fit",{"normal","wide"},"normal"});NodePatch variant;variant.imageFit="stretch";def.rules={{{{"fit","wide"}},n.id,variant}};REQUIRE(f.editor.setComponentDefinition(main.id,def));
    NodePatch chosen;chosen.componentValues=ComponentValues{{"fit",std::string("wide")}};REQUIRE(f.editor.setProps(instance,chosen));REQUIRE(f.session.document().view(copy)->imageFit=="contain");REQUIRE(f.editor.resetOverrides(copy));REQUIRE(f.session.document().view(copy)->imageFit=="stretch");
}
TEST_CASE("Images reject invalid pixels, crop, foreign fields and decoration atomically") {
    Images f;auto n=f.node();REQUIRE(f.session.edit(CreateNode{n}));const auto before=snapshotToJson(f.session.document());
    for(int i=0;i<9;++i){NodePatch p;
        if(i==0){p.image=n.image;p.image->id[0]='!';}if(i==1)p.imageFit="tile";if(i==2)p.imageCrop=ImageCrop{.5,0,.6,1};if(i==3)p.imageCrop=ImageCrop{0,0,0,1};
        if(i==4)p.stroke=Stroke{2.};if(i==5)p.widthSizing=Sizing::Hug;if(i==6)p.clipContent=true;if(i==7)p.lineWidth=2;if(i==8)p.cornerRadius=2.;
        REQUIRE_FALSE(f.editor.setProps(n.id,p));REQUIRE(snapshotToJson(f.session.document())==before);
    }
    Node rect;rect.id=f.session.newId();rect.order="b0";REQUIRE(f.session.edit(CreateNode{rect}));NodePatch p;p.image=n.image;REQUIRE_FALSE(f.editor.setProps(rect.id,p));p={};p.imageFit="cover";REQUIRE_FALSE(f.editor.setProps(rect.id,p));
    REQUIRE_FALSE(patchFromJson(R"({"image":{"width":1,"height":1,"pixels":"/wAAgA==","id":"c1034074bbdc364cba7be6af4f2cccd5b4083577bb4ca69ceb571385757a8405","url":"https://example.test"}})").has_value());
}
TEST_CASE("Raster document budgets reject ninth distinct asset and duplicate large authored payload atomically") {
    Images f;for(int i=0;i<8;++i){auto n=f.node();n.image=raster(1,1,std::uint8_t(i));REQUIRE(f.session.edit(CreateNode{n}));}
    const auto before=snapshotToJson(f.session.document());auto ninth=f.node();ninth.image=raster(1,1,9);REQUIRE_FALSE(f.session.edit(CreateNode{ninth}));REQUIRE(snapshotToJson(f.session.document())==before);
    Images large;auto first=large.node();first.image=raster(512,512);REQUIRE(large.session.edit(CreateNode{first}));const auto previous=snapshotToJson(large.session.document());auto second=large.node();second.image=first.image;
    REQUIRE_FALSE(large.session.edit(CreateNode{second}));REQUIRE(snapshotToJson(large.session.document())==previous);REQUIRE(large.editor.undo());REQUIRE(large.session.document().size()==0);REQUIRE(large.editor.redo());REQUIRE(large.session.document().find(first.id)->image==first.image);
}

TEST_CASE("Raster upload premultiplies color before filtering and preserves immutable straight-alpha assets") {
    const std::vector<std::uint8_t> straight{255,128,64,128,255,255,255,0,255,0,0,255};
    const auto upload=premultiplyRasterPixels(straight);
    REQUIRE(upload==std::vector<std::uint8_t>{128,64,32,128,0,0,0,0,255,0,0,255});
    REQUIRE(straight[0]==255);REQUIRE(straight[4]==255);REQUIRE(premultiplyRasterPixels({1,2,3}).empty());
    // Halfway between opaque red and transparent black stays red at half coverage;
    // straight-alpha texture interpolation followed directly by SrcAlpha would darken it.
    const auto edge=premultiplyRasterPixels({255,0,0,255,0,0,0,0});
    const double filteredRed=(edge[0]+edge[4])/(2.*255),filteredAlpha=(edge[3]+edge[7])/(2.*255);
    const double recoveredRed=filteredRed/filteredAlpha;
    REQUIRE(recoveredRed==1);REQUIRE(recoveredRed*filteredAlpha==.5);
    Raster image{3,1,encodeRasterPixels(straight),rasterDigest(3,1,straight)};
    REQUIRE(validRaster(image));REQUIRE(*decodeRaster(image)==straight);
}
