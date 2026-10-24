#include <catch2/catch_test_macros.hpp>
#include "vc/editor.hpp"
#include "vc/geometry.hpp"
#include "vc/json.hpp"

using namespace vc;

namespace {
Raster pixels(int width=2,int height=1,std::uint8_t value=127) {
    const std::vector<std::uint8_t> bytes(static_cast<std::size_t>(width)*height*4,value);
    return Raster{width,height,encodeRasterPixels(bytes),rasterDigest(width,height,bytes)};
}
struct Images {
    Session session{IdGenerator{153}};
    Camera camera;
    Editor editor{session,camera};
    Node frame(NodeType type=NodeType::Frame) {
        Node node;node.id=session.newId();node.type=type;node.order="a0";
        node.x=100;node.y=200;node.w=300;node.h=200;
        return node;
    }
};
}

TEST_CASE("Importing local raster pixels creates one undoable intrinsic-size image at the camera origin") {
    Images f;f.camera.pan({-200,-100});
    const auto image=pixels();const auto id=f.editor.importImage(image,"Local image");
    REQUIRE(id.has_value());
    const auto* node=f.session.document().find(*id);
    REQUIRE(node->type==NodeType::Image);REQUIRE(node->name=="Local image");
    REQUIRE(node->image==image);REQUIRE(node->w==2);REQUIRE(node->h==1);
    REQUIRE(node->x==f.camera.position().x+40);REQUIRE(node->y==f.camera.position().y+40);
    REQUIRE(node->parent.isNil());REQUIRE(node->absolute);
    REQUIRE(f.editor.selection()==std::vector<Id>{*id});
    REQUIRE(f.editor.tool()==Tool::Select);REQUIRE(f.session.takeOutbox().size()==1);
    REQUIRE(f.editor.undo());REQUIRE(f.session.document().size()==0);
    REQUIRE_FALSE(f.editor.undo());REQUIRE(f.editor.redo());
    REQUIRE(f.session.document().find(*id)->image==image);
}

TEST_CASE("Import targets selected source frames and components while copied layers stay untouched") {
    for(const auto type:{NodeType::Frame,NodeType::Component}) {
        Images f;const auto parent=f.frame(type);REQUIRE(f.session.edit(CreateNode{parent}));
        f.editor.select(parent.id);const auto id=f.editor.importImage(pixels(),"");
        REQUIRE(id.has_value());const auto* node=f.session.document().find(*id);
        REQUIRE(node->parent==parent.id);REQUIRE(node->x==20);REQUIRE(node->y==20);REQUIRE(node->name=="Image");
        const auto bounds=worldBounds(f.session.document(),*id);
        REQUIRE(bounds->x==120);REQUIRE(bounds->y==220);
        REQUIRE(f.editor.undo());REQUIRE(f.session.document().size()==1);REQUIRE(f.editor.redo());
        if(type==NodeType::Component) {
            const auto instance=f.editor.createInstance(parent.id);REQUIRE(instance.has_value());
            const auto copy=f.session.document().viewChildren(*instance).front();
            f.editor.select(copy);
            const auto imported=f.editor.importImage(pixels(1,2),"Canvas image");
            REQUIRE(imported.has_value());REQUIRE(f.session.document().find(*imported)->parent.isNil());
            REQUIRE(f.session.document().find(*instance)->overrides.empty());
            REQUIRE(f.session.document().view(copy)->image==pixels());
        }
    }
}

TEST_CASE("Invalid or over-budget imports refuse atomically without changing selection or undo history") {
    Images f;const auto frame=f.frame();REQUIRE(f.session.edit(CreateNode{frame}));f.editor.select(frame.id);
    static_cast<void>(f.session.takeOutbox());
    const auto before=snapshotToJson(f.session.document());
    auto invalid=pixels();invalid.id=std::string(64,'0');
    REQUIRE_FALSE(f.editor.importImage(invalid,"Invalid").has_value());
    invalid=pixels();invalid.pixels.pop_back();
    REQUIRE_FALSE(f.editor.importImage(invalid,"Truncated").has_value());
    REQUIRE(snapshotToJson(f.session.document())==before);REQUIRE(f.session.takeOutbox().empty());
    REQUIRE(f.editor.selection()==std::vector<Id>{frame.id});
    const auto full=f.editor.importImage(pixels(512,512),"Full");REQUIRE(full.has_value());
    static_cast<void>(f.session.takeOutbox());
    const auto fullSnapshot=snapshotToJson(f.session.document());
    REQUIRE_FALSE(f.editor.importImage(pixels(32,32),"Over budget").has_value());
    REQUIRE(snapshotToJson(f.session.document())==fullSnapshot);REQUIRE(f.session.takeOutbox().empty());
    REQUIRE(f.editor.selection()==std::vector<Id>{*full});
    REQUIRE(f.editor.undo());REQUIRE(f.session.document().size()==1);
}

TEST_CASE("Image import cancels pending pen and resize previews and refuses a locked destination") {
    Images f;const auto frame=f.frame();REQUIRE(f.session.edit(CreateNode{frame}));
    f.editor.setTool(Tool::Pen);f.editor.pointerDown({500,500},PointerButton::Primary);f.editor.pointerUp();
    f.editor.pointerDown({600,600},PointerButton::Primary);f.editor.pointerUp();
    REQUIRE(f.editor.drawingPath());
    const auto image=f.editor.importImage(pixels(),"Image");REQUIRE(image.has_value());
    REQUIRE_FALSE(f.editor.drawingPath());REQUIRE(f.session.document().size()==2);
    f.editor.select(frame.id);f.editor.pointerDown({100,200},PointerButton::Primary);
    f.editor.pointerMove({80,180});
    const auto second=f.editor.importImage(pixels(),"Second");REQUIRE(second.has_value());
    REQUIRE(f.session.document().find(frame.id)->x==100);REQUIRE(f.session.document().find(frame.id)->y==200);
    REQUIRE(f.editor.undo());REQUIRE(f.session.document().size()==2);
    NodePatch locked;locked.locked=true;REQUIRE(f.editor.setProps(frame.id,locked));
    f.editor.select(frame.id);static_cast<void>(f.session.takeOutbox());
    const auto before=snapshotToJson(f.session.document());
    REQUIRE_FALSE(f.editor.importImage(pixels(),"Refused").has_value());
    REQUIRE(snapshotToJson(f.session.document())==before);REQUIRE(f.session.takeOutbox().empty());
}
