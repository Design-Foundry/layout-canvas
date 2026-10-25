#include "vc/editor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>
#include "vc/fractional_index.hpp"
#include "vc/geometry.hpp"

namespace vc {
namespace {

constexpr std::array<std::pair<Tool, std::string_view>, 11> kToolNames{{
    {Tool::Select, "select"},
    {Tool::Frame, "frame"},
    {Tool::Rect, "rectangle"},
    {Tool::Ellipse, "ellipse"},
    {Tool::Hand, "hand"},
    {Tool::Text, "text"},
    {Tool::Line, "line"},
    {Tool::Pen, "pen"},
    {Tool::Pencil, "pencil"},
    {Tool::Polygon, "polygon"},
    {Tool::Star, "star"},
}};

bool isDrawTool(Tool tool) {
    return tool == Tool::Frame || tool == Tool::Rect || tool == Tool::Ellipse || tool == Tool::Text || tool == Tool::Line || tool == Tool::Polygon || tool == Tool::Star;
}

Vec2 constrainedLine(Vec2 d) {
    constexpr double step=3.14159265358979323846/4;
    const double angle=std::round(std::atan2(d.y,d.x)/step)*step;
    const double length=std::hypot(d.x,d.y);
    return {length*std::cos(angle),length*std::sin(angle)};
}

double distance(Vec2 a, Vec2 b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}

Node newShape(Tool tool) {
    Node node;
    switch (tool) {
    case Tool::Pencil:
        node.type=NodeType::Path; node.name="Pencil"; node.fill=Oklch{0.95,0,0}; node.absolute=true;
        break;
    case Tool::Polygon:
    case Tool::Star:
        node.type=NodeType::Path; node.name=tool==Tool::Star ? "Star" : "Polygon";
        node.fill=Oklch{0.89,0,0}; node.absolute=true;
        break;
    case Tool::Pen:
        node.type=NodeType::Path; node.name="Path"; node.fill=Oklch{0.95,0,0}; node.absolute=true;
        break;
    case Tool::Line:
        node.type=NodeType::Line; node.name="Line"; node.fill=Oklch{0.95,0,0}; node.absolute=true;
        break;
    case Tool::Text:
        node.type = NodeType::Text;
        node.name = "Text";
        node.text.content = "Texto";
        node.text.fontSize = 24;
        node.fill = Oklch{0.95, 0.0, 0.0};
        node.widthSizing = Sizing::Hug;
        node.heightSizing = Sizing::Hug;
        break;
    case Tool::Frame:
        node.type = NodeType::Frame;
        node.name = "Frame";
        node.fill = Oklch{1.0, 0.0, 0.0};
        node.clipContent = true;
        break;
    case Tool::Ellipse:
        node.type = NodeType::Ellipse;
        node.name = "Ellipse";
        node.fill = Oklch{0.89, 0.0, 0.0};
        break;
    default:
        node.type = NodeType::Rect;
        node.name = "Rectangle";
        node.fill = Oklch{0.89, 0.0, 0.0};
        break;
    }
    return node;
}

} // namespace

std::optional<Tool> toolFromName(std::string_view name) {
    for (const auto& [tool, text] : kToolNames) {
        if (text == name) return tool;
    }
    return std::nullopt;
}

Editor::Editor(Session& session, Camera& camera) : session_(session), camera_(camera), viewport_(camera) {}

bool Editor::prototypeEligible(const Id& id) const {
    const auto* node=session_.document().find(id);
    if(!node || node->type!=NodeType::Frame || !node->parent.isNil() || !node->visible) return false;
    const auto box=worldBounds(session_.document(),id,activeMode_);
    return box && std::isfinite(box->x) && std::isfinite(box->y) && std::isfinite(box->w) &&
        std::isfinite(box->h) && std::isfinite(box->x+box->w) && std::isfinite(box->y+box->h) && box->w>0 && box->h>0;
}

bool Editor::startPrototype(const Id& screen,Vec2 viewport) {
    if(!prototypeEligible(screen) || !std::isfinite(viewport.x) || !std::isfinite(viewport.y) || viewport.x<=0 || viewport.y<=0) return false;
    if(!presenting_) {
        pointerCancel();
        cancelPropsPreview();
        if(!prototypeEligible(screen)) return false;
        prototypeSavedCamera_=camera_;
    }
    presenting_=true;
    prototypeScreen_=prototypeStart_=screen;
    prototypeViewport_=viewport;
    prototypeHistory_.clear();prototypeOverlays_.clear();cancelPrototypeTransition();
    prototypeClick_=false;prototypeOutsidePressed_=false;
    fitPrototype();
    touch();
    return true;
}

void Editor::stopPrototype() {
    if(!presenting_) return;
    camera_=prototypeSavedCamera_;
    presenting_=false;
    prototypeScreen_={};prototypeStart_={};
    prototypeHistory_.clear();prototypeOverlays_.clear();cancelPrototypeTransition();
    prototypeClick_=false;prototypeOutsidePressed_=false;
    touch();
}

void Editor::fitPrototype() {
    if(!presenting_) return;
    const auto bounds=worldBounds(session_.document(),prototypeScreen_,activeMode_);
    if(!bounds) return;
    const double zoom=std::clamp(std::min(std::max(1.,prototypeViewport_.x-48)/bounds->w,
        std::max(1.,prototypeViewport_.y-48)/bounds->h),Camera::kMinZoom,Camera::kMaxZoom);
    camera_=Camera{};
    camera_.zoomAt({},zoom);
    const Vec2 position{bounds->x+bounds->w/2-prototypeViewport_.x/(2*zoom),
                        bounds->y+bounds->h/2-prototypeViewport_.y/(2*zoom)};
    camera_.pan({-position.x*zoom,-position.y*zoom});
}

void Editor::resizePrototype(Vec2 viewport) {
    if(!presenting_) return;
    if(!prototypeEligible(prototypeScreen_) || !prototypeEligible(prototypeStart_)) {stopPrototype();return;}
    if(!std::isfinite(viewport.x) || !std::isfinite(viewport.y) || viewport.x<=0 || viewport.y<=0) {prototypeClick_=false;return;}
    const auto changed=[](const std::optional<Rect>& a,const std::optional<Rect>& b) {
        return a.has_value()!=b.has_value() || (a && (a->x!=b->x || a->y!=b->y || a->w!=b->w || a->h!=b->h));
    };
    if(prototypeTransitioning() && (viewport.x!=prototypeViewport_.x || viewport.y!=prototypeViewport_.y ||
        changed(prototypeTransitionBase_,worldBounds(session_.document(),prototypeScreen_,activeMode_)) ||
        (prototypeOverlayTransition_ && changed(prototypeTransitionTarget_,worldBounds(session_.document(),prototypeOverlay(),activeMode_))) ||
        (!prototypeOverlayTransition_ && !prototypeOutgoing_.empty() && changed(prototypeTransitionTarget_,worldBounds(session_.document(),prototypeOutgoing_.front().screen,activeMode_)))))
        finishPrototypeTransition();
    for(std::size_t i=0;i<prototypeOverlays_.size();++i) {
        if(!prototypeEligible(prototypeOverlays_[i].screen)) {
            prototypeOverlays_.resize(i);cancelPrototypeTransition();prototypeClick_=false;touch();break;
        }
    }
    if(std::any_of(prototypeOutgoing_.begin(),prototypeOutgoing_.end(),[&](const auto& layer){return !prototypeEligible(layer.screen);})) cancelPrototypeTransition();
    const auto oldPosition=camera_.position();const auto oldZoom=camera_.zoom();
    prototypeViewport_=viewport;
    fitPrototype();
    if(oldPosition.x!=camera_.position().x || oldPosition.y!=camera_.position().y || oldZoom!=camera_.zoom()) touch();
}

bool Editor::prototypeBack() {
    if(!presenting_) return false;
    resizePrototype(prototypeViewport_);
    if(!presenting_) return false;
    cancelPrototypeTransition();
    if(!prototypeOverlays_.empty()) return closePrototypeOverlay();
    while(!prototypeHistory_.empty()) {
        const auto previous=prototypeHistory_.back();prototypeHistory_.pop_back();
        if(!prototypeEligible(previous)) continue;
        prototypeScreen_=previous;prototypeClick_=false;fitPrototype();touch();return true;
    }
    touch();return false;
}

bool Editor::restartPrototype() {
    if(!presenting_) return false;
    resizePrototype(prototypeViewport_);
    if(!presenting_) return false;
    prototypeScreen_=prototypeStart_;prototypeHistory_.clear();prototypeOverlays_.clear();cancelPrototypeTransition();prototypeClick_=false;
    fitPrototype();touch();return true;
}

Id Editor::prototypeOverlay() const {
    return prototypeOverlays_.empty()?Id{}:prototypeOverlays_.back().screen;
}

Vec2 Editor::prototypeOffset(const Id& screen) const {
    if(screen==prototypeScreen_) return {};
    const auto base=worldBounds(session_.document(),prototypeScreen_,activeMode_);
    const auto box=worldBounds(session_.document(),screen,activeMode_);
    if(!base || !box) return {};
    const auto found=std::find_if(prototypeOverlays_.begin(),prototypeOverlays_.end(),[&](const auto& overlay){return overlay.screen==screen;});
    const auto placement=found==prototypeOverlays_.end()?OverlayPlacement::Center:found->placement;
    double x=.5,y=.5;
    switch(placement) {
    case OverlayPlacement::TopLeft:x=0;y=0;break;
    case OverlayPlacement::TopCenter:y=0;break;
    case OverlayPlacement::TopRight:x=1;y=0;break;
    case OverlayPlacement::CenterLeft:x=0;break;
    case OverlayPlacement::CenterRight:x=1;break;
    case OverlayPlacement::BottomLeft:x=0;y=1;break;
    case OverlayPlacement::BottomCenter:y=1;break;
    case OverlayPlacement::BottomRight:x=1;y=1;break;
    case OverlayPlacement::Manual:
        return {base->x+found->offset.x-box->x,base->y+found->offset.y-box->y};
    case OverlayPlacement::Center:break;
    }
    return {base->x+(base->w-box->w)*x-box->x,base->y+(base->h-box->h)*y-box->y};
}

void Editor::cancelPrototypeTransition() {
    prototypeOutgoing_.clear();prototypeDuration_=prototypeElapsed_=0;prototypeOverlayTransition_=false;prototypeTransition_=PrototypeTransition::Instant;
    prototypeTransitionBase_.reset();prototypeTransitionTarget_.reset();
}

void Editor::finishPrototypeTransition() {
    if(!prototypeTransitioning()) return;
    cancelPrototypeTransition();prototypeClick_=false;prototypeOutsidePressed_=false;prototypePressed_={};touch();
}

void Editor::setPrototypeReducedMotion(bool reduced) {
    prototypeReducedMotion_=reduced;
    if(reduced) finishPrototypeTransition();
}

void Editor::advancePrototype(double deltaMs) {
    if(!presenting_) return;
    resizePrototype(prototypeViewport_);
    if(!prototypeTransitioning() || !std::isfinite(deltaMs) || deltaMs<=0) return;
    prototypeElapsed_=std::min(prototypeDuration_,prototypeElapsed_+deltaMs);
    if(prototypeElapsed_>=prototypeDuration_) cancelPrototypeTransition();
    touch();
}

std::vector<PrototypeLayer> Editor::prototypeLayers() const {
    if(!presenting_) return {};
    const bool moving=prototypeTransition_!=PrototypeTransition::Instant && prototypeTransition_!=PrototypeTransition::Dissolve;
    const bool push=prototypeTransition_==PrototypeTransition::PushLeft || prototypeTransition_==PrototypeTransition::PushRight ||
        prototypeTransition_==PrototypeTransition::PushUp || prototypeTransition_==PrototypeTransition::PushDown;
    const double progress=prototypeTransitioning()?prototypeElapsed_/prototypeDuration_:1;
    Vec2 motion{};
    if(moving) {
        const auto base=worldBounds(session_.document(),prototypeScreen_,activeMode_);
        const auto target=prototypeOverlayTransition_?worldBounds(session_.document(),prototypeOverlay(),activeMode_):base;
        if(base && target) {
            const auto offset=prototypeOverlayTransition_?prototypeOffset(prototypeOverlay()):Vec2{};
            const bool left=prototypeTransition_==PrototypeTransition::SlideLeft || prototypeTransition_==PrototypeTransition::PushLeft;
            const bool right=prototypeTransition_==PrototypeTransition::SlideRight || prototypeTransition_==PrototypeTransition::PushRight;
            const bool up=prototypeTransition_==PrototypeTransition::SlideUp || prototypeTransition_==PrototypeTransition::PushUp;
            if(left) motion.x=prototypeOverlayTransition_?std::max(0.,base->x+base->w-target->x-offset.x):base->w;
            else if(right) motion.x=prototypeOverlayTransition_?std::min(0.,base->x-target->x-offset.x-target->w):-base->w;
            else if(up) motion.y=prototypeOverlayTransition_?std::max(0.,base->y+base->h-target->y-offset.y):base->h;
            else motion.y=prototypeOverlayTransition_?std::min(0.,base->y-target->y-offset.y-target->h):-base->h;
        }
    }
    const auto incoming=Vec2{motion.x*(1-progress),motion.y*(1-progress)};
    std::vector<PrototypeLayer> layers;
    if(!prototypeOverlayTransition_) {
        for(auto layer:prototypeOutgoing_) {
            if(moving) {if(push) layer.offset=layer.offset-Vec2{motion.x*progress,motion.y*progress};}
            else layer.opacity*=1-progress;
            layers.push_back(layer);
        }
    }
    const auto stage=worldBounds(session_.document(),prototypeScreen_,activeMode_);
    layers.push_back({prototypeScreen_,prototypeOverlayTransition_?Vec2{}:incoming,
        prototypeOverlayTransition_ || moving?1:progress,false,1,stage});
    for(std::size_t i=0;i<prototypeOverlays_.size();++i) {
        const auto id=prototypeOverlays_[i].screen;
        const bool entering=prototypeOverlayTransition_ && i+1==prototypeOverlays_.size();
        layers.push_back({id,prototypeOffset(id)+(entering?incoming:Vec2{}),entering && !moving?progress:1,true,1,stage});
    }
    return layers;
}

PrototypeScene Editor::prototypeScene() const {
    PrototypeScene scene;
    scene.to=prototypeLayers();
    scene.dissolve=prototypeTransitioning() && prototypeTransition_==PrototypeTransition::Dissolve;
    if(!scene.dissolve) return scene;
    scene.progress=prototypeElapsed_/prototypeDuration_;
    if(prototypeOverlayTransition_) {
        scene.from=scene.to;
        if(!scene.from.empty()) scene.from.pop_back();
    } else {
        scene.from=prototypeOutgoing_;
        scene.to.erase(scene.to.begin(),scene.to.begin()+static_cast<std::ptrdiff_t>(prototypeOutgoing_.size()));
    }
    for(auto* layers:{&scene.from,&scene.to}) for(auto& layer:*layers) layer.opacity=1;
    return scene;
}

bool Editor::closePrototypeOverlay() {
    if(!presenting_ || prototypeOverlays_.empty()) return false;
    prototypeOverlays_.pop_back();cancelPrototypeTransition();prototypeClick_=false;prototypeOutsidePressed_=false;touch();return true;
}

bool Editor::prototypeOutsideOverlay(Vec2 screen) const {
    if(prototypeOverlays_.empty() || !prototypeOverlays_.back().dismissOutside) return false;
    const auto base=worldBounds(session_.document(),prototypeScreen_,activeMode_);
    if(!base || !base->contains(camera_.toWorld(screen))) return true;
    const auto overlay=prototypeOverlays_.back().screen;
    const auto bounds=worldBounds(session_.document(),overlay,activeMode_);
    return bounds && !bounds->contains(camera_.toWorld(screen)-prototypeOffset(overlay));
}

Id Editor::prototypeHotspot(Vec2 screen) const {
    if(prototypeTransitioning()) return {};
    const auto& document=session_.document();
    const auto stage=worldBounds(document,prototypeScreen_,activeMode_);
    if(!stage || !stage->contains(camera_.toWorld(screen))) return {};
    const auto root=prototypeOverlays_.empty()?prototypeScreen_:prototypeOverlays_.back().screen;
    for(Id id=hitTest(document,camera_.toWorld(screen)-prototypeOffset(root),false,activeMode_,4/camera_.zoom(),root,true);
        !id.isNil();) {
        const auto* node=document.view(id);
        if(!node) return {};
        if(node->interaction.action!=PrototypeAction::None) return id;
        if(id==root) break;
        id=node->parent;
    }
    return {};
}

bool Editor::activatePrototype(Vec2 screen) {
    const auto* node=session_.document().view(prototypeHotspot(screen));
    if(!node) return false;
    const auto interaction=node->interaction;
    if(interaction.action==PrototypeAction::Back) return prototypeBack();
    if(interaction.action==PrototypeAction::CloseOverlay) return closePrototypeOverlay();
    if(interaction.action==PrototypeAction::Navigate || interaction.action==PrototypeAction::OpenOverlay) {
        if(!prototypeEligible(interaction.target)) return false;
        const bool overlay=interaction.action==PrototypeAction::OpenOverlay;
        if(overlay && (prototypeOverlays_.size()>=8 || interaction.target==prototypeScreen_ ||
            std::any_of(prototypeOverlays_.begin(),prototypeOverlays_.end(),[&](const auto& item){return item.screen==interaction.target;}))) return false;
        if(!overlay && interaction.target==prototypeScreen_) {
            if(prototypeOverlays_.empty()) return false;
            prototypeOverlays_.clear();cancelPrototypeTransition();prototypeClick_=false;prototypeOutsidePressed_=false;touch();return true;
        }
        const auto old=worldBounds(session_.document(),prototypeScreen_,activeMode_);
        const auto outgoingCamera=camera_;
        cancelPrototypeTransition();
        if(overlay) {
            prototypeOverlays_.push_back({interaction.target,interaction.dismissOutside,interaction.placement,interaction.overlayOffset});
        } else {
            if(prototypeHistory_.size()>=128) prototypeHistory_.erase(prototypeHistory_.begin());
            prototypeHistory_.push_back(prototypeScreen_);
            const auto previous=prototypeScreen_;
            prototypeScreen_=interaction.target;prototypeOverlays_.clear();fitPrototype();
            const auto next=worldBounds(session_.document(),prototypeScreen_,activeMode_);
            if(old && next) {
                const auto scale=outgoingCamera.zoom()/camera_.zoom();
                const Vec2 offset{camera_.position().x-outgoingCamera.position().x*scale,
                                  camera_.position().y-outgoingCamera.position().y*scale};
                const Rect viewport{old->x*scale+offset.x,old->y*scale+offset.y,old->w*scale,old->h*scale};
                prototypeOutgoing_.push_back({previous,offset,1,false,scale,viewport});
            }
        }
        if(interaction.transition!=PrototypeTransition::Instant && !prototypeReducedMotion_) {
            prototypeDuration_=interaction.durationMs;prototypeElapsed_=0;prototypeOverlayTransition_=overlay;prototypeTransition_=interaction.transition;
            prototypeTransitionBase_=worldBounds(session_.document(),prototypeScreen_,activeMode_);
            prototypeTransitionTarget_=overlay?worldBounds(session_.document(),prototypeOverlay(),activeMode_):old;
        } else cancelPrototypeTransition();
        prototypeClick_=false;prototypeOutsidePressed_=false;touch();return true;
    }
    return false;
}

bool Editor::edit(const OpBody& op) {
    if(presenting_) return false;
    if (gesture_ != Gesture::None) {
        return false;
    }
    settlePropsPreview();
    return session_.edit(op);
}

std::optional<Id> Editor::addToken(TokenType type) {
    if(presenting_) return std::nullopt;
    const Document& document = session_.document();
    std::string lastOrder;
    std::unordered_set<std::string> taken;
    for (const Token* token : document.tokens()) {
        if (token->type == type) {
            lastOrder = token->order;
            taken.insert(token->identifier);
        }
    }

    Token token;
    token.id = session_.newId();
    token.type = type;
    token.order = keyBetween(lastOrder, std::nullopt);
    for (int n = 1; token.identifier.empty() || taken.contains(token.identifier); ++n) {
        token.identifier = std::string(tokenTypeName(type)) + "-" + std::to_string(n);
    }
    switch (type) {
    case TokenType::Typography:
        token.value = TextStyle{16.0, 1.4, 0.0, TextAlign::Left, std::string("inter-regular-v1")};
        break;
    case TokenType::Color:
        token.value = Oklch{0.6, 0.0, 0.0};
        break;
    case TokenType::Dimension:
        token.value = Magnitude{8.0, Unit::Px};
        break;
    case TokenType::Duration:
        token.value = Magnitude{200.0, Unit::Ms};
        break;
    case TokenType::Ratio:
    case TokenType::Opacity:
        token.value = Magnitude{1.0, Unit::None};
        break;
    case TokenType::Border:
    case TokenType::Shadow: {
        const Token* firstColor = nullptr;
        const Token* firstDimension = nullptr;
        for (const Token* other : document.tokens()) {
            if (!firstColor && other->type == TokenType::Color) firstColor = other;
            if (!firstDimension && other->type == TokenType::Dimension) firstDimension = other;
        }
        if (!firstColor || (type == TokenType::Border && !firstDimension)) {
            return std::nullopt;
        }
        if (type == TokenType::Border) {
            token.value = BorderValue{firstDimension->id, StrokeStyle::Solid, firstColor->id};
        } else {
            token.value = ShadowValue{{ShadowLayerValue{0.0, 4.0, 8.0, 0.0, false, firstColor->id, {}}}};
        }
        break;
    }
    }
    const Id id = token.id;
    return edit(CreateToken{std::move(token)}) ? std::optional{id} : std::nullopt;
}

std::optional<Id> Editor::addMode() {
    if(presenting_) return std::nullopt;
    const auto modes = session_.document().modes();
    Mode mode{session_.newId(), "Mode " + std::to_string(modes.size() + 1), keyBetween(modes.back()->order, std::nullopt)};
    const Id id = mode.id;
    return edit(CreateMode{std::move(mode)}) ? std::optional{id} : std::nullopt;
}

bool Editor::deleteMode(const Id& mode) {
    if(presenting_) return false;
    if (gesture_ != Gesture::None) {
        return false;
    }
    settlePropsPreview();
    const bool deleted = session_.deleteMode(mode);
    if (deleted && activeMode_ == mode) {
        activeMode_ = kDefaultModeId;
        touch();
    }
    return deleted;
}

void Editor::setActiveMode(const Id& mode) {
    if(presenting_) return;
    if (mode != activeMode_ && session_.document().findMode(mode)) {
        cancelPath();
        activeMode_ = mode;
        touch();
    }
}

void Editor::setTool(Tool tool) {
    if(presenting_) return;
    if (tool != tool_) {
        if (drawingPath()) cancelPath();
        if (gesture_ != Gesture::None) pointerCancel();
        tool_ = tool;
        touch();
    }
}

void Editor::pointerDown(Vec2 screen, PointerButton button, Modifiers modifiers) {
    if(presenting_) {
        resizePrototype(prototypeViewport_);
        if(presenting_) {
            downScreen_=lastScreen_=screen;
            prototypePressed_=button==PointerButton::Primary?prototypeHotspot(screen):Id{};
            prototypeOutsidePressed_=button==PointerButton::Primary && !prototypeTransitioning() && prototypeOutsideOverlay(screen);
            prototypeClick_=!prototypePressed_.isNil() || prototypeOutsidePressed_;
        }
        return;
    }
    if (gesture_ != Gesture::None) {
        return;
    }
    if (!drawingPath()) settlePropsPreview();
    modifiers_ = modifiers;
    lastScreen_ = screen;
    downScreen_ = screen;
    downWorld_ = camera_.toWorld(screen);

    if (button == PointerButton::Middle || (button == PointerButton::Primary && tool_ == Tool::Hand)) {
        gesture_ = Gesture::Pan;
        viewport_.pointerDown(screen);
    } else if (button != PointerButton::Primary) {
        return;
    } else if (tool_ == Tool::Pen) {
        addPathPoint(downWorld_);
    } else if (tool_ == Tool::Pencil) {
        clearSelection();
        target_ = session_.newId();
        drawParent_ = frameAt(session_.document(), downWorld_, activeMode_, {});
        parentOrigin_ = originOf(drawParent_);
        pencilPoints_ = {{snap(downWorld_.x),snap(downWorld_.y)}};
        gesture_ = Gesture::Pencil;
    } else if (isDrawTool(tool_)) {
        startDraw(downWorld_);
    } else if (const Id label = frameLabelAt(screen); !label.isNil()) {
        if (modifiers_.shift) {
            toggleSelection(label);
        } else {
            if (!isSelected(label)) select(label);
            beginMoving();
        }
    } else if (const Handle handle = handleAt(screen); !handle.isNone()) {
        startResize(handle);
    } else {
        startMove(downWorld_);
    }
}

void Editor::doubleClick(Vec2 screen) {
    if(presenting_) return;
    if (tool_ == Tool::Pen && drawingPath()) {
        finishPath();
        return;
    }
    if (gesture_ != Gesture::None || tool_ != Tool::Select || selection_.size() != 1) {
        return;
    }
    const Document& document = session_.document();
    for (Id id = hitTest(document, camera_.toWorld(screen), false, activeMode_,4/camera_.zoom()); !id.isNil();
         id = document.view(id)->parent) {
        if (document.view(id)->parent == selection_[0]) {
            select(id);
            return;
        }
    }
}

void Editor::pointerMove(Vec2 screen, Modifiers modifiers) {
    if(presenting_) {
        lastScreen_=screen;
        if(distance(screen,downScreen_)>=kClickSlopPx) prototypeClick_=false;
        return;
    }
    modifiers_ = modifiers;
    lastScreen_ = screen;
    if (drawingPath() && gesture_ == Gesture::None) {
        previewPath(true);
        return;
    }
    update();
}

void Editor::setModifiers(Modifiers modifiers) {
    if(presenting_) return;
    if (modifiers.shift == modifiers_.shift && modifiers.alt == modifiers_.alt) {
        return;
    }
    modifiers_ = modifiers;
    if (drawingPath() && gesture_ == Gesture::None) previewPath(true);
    if (gesture_ == Gesture::Draw || gesture_ == Gesture::Move || gesture_ == Gesture::Resize || gesture_ == Gesture::PenCurve) {
        update();
    }
}

void Editor::update() {
    const Vec2 screen = lastScreen_;
    const Vec2 world = camera_.toWorld(screen);
    switch (gesture_) {
    case Gesture::PenCurve:
        updatePenCurve(world);
        break;
    case Gesture::Pencil:
        updatePencil(world);
        break;
    case Gesture::Pan:
        viewport_.pointerMove(screen);
        break;
    case Gesture::Draw:
        updateDraw(world, screen);
        break;
    case Gesture::Move:
        if (distance(screen, downScreen_) >= kClickSlopPx || session_.hasPreview()) {
            updateMove(world);
        }
        break;
    case Gesture::Resize:
        updateResize(world);
        break;
    case Gesture::Marquee:
        updateMarquee(world);
        break;
    case Gesture::None:
        break;
    }
}

void Editor::pointerUp() {
    if(presenting_) {
        const bool click=std::exchange(prototypeClick_,false);
        const auto pressed=std::exchange(prototypePressed_,Id{});
        const bool outside=std::exchange(prototypeOutsidePressed_,false);
        resizePrototype(prototypeViewport_);
        if(presenting_ && click && !prototypeTransitioning()) {
            if(outside && prototypeOutsideOverlay(lastScreen_)) closePrototypeOverlay();
            else if(!outside && !pressed.isNil() && pressed==prototypeHotspot(lastScreen_)) activatePrototype(lastScreen_);
        }
        return;
    }
    finish();
}

void Editor::pointerCancel() {
    if(presenting_) {prototypeClick_=false;prototypeOutsidePressed_=false;prototypePressed_={};return;}
    cancelPath();
    if (gesture_ == Gesture::Pan) {
        viewport_.pointerUp();
    }
    session_.cancelPreview();
    marquee_.reset();
    gesture_ = Gesture::None;
    pencilPoints_.clear();
}

void Editor::wheel(Vec2 screen, double deltaY) {
    if(presenting_) return;
    viewport_.wheel(screen, deltaY);
}

std::vector<OpBody> Editor::propsOps(const Id& id, const NodePatch& patch) const {
    if (patch.text && patch.textStyle) return {};
    if (patch.textStyle && *patch.textStyle == TextStyle{}) return {};
    const Document& document = session_.document();
    const auto textChanges = [](const TextRun& before, const TextRun& after) {
        TextStyle out;
        if (before.font != after.font) out.font = after.font;
        if (before.fontSize != after.fontSize) out.fontSize = after.fontSize;
        if (before.lineHeight != after.lineHeight) out.lineHeight = after.lineHeight;
        if (before.letterSpacing != after.letterSpacing) out.letterSpacing = after.letterSpacing;
        if (before.align != after.align) out.align = after.align;
        return out;
    };
    const auto existing = [](const Node& instance, const Id& key) {
        for (const Override& o : instance.overrides) {
            if (o.node == key) return o.patch;
        }
        return NodePatch{};
    };
    if (isCopy(id)) {
        if (patch.typography) return {};
        const Node* instance = document.find(document.ownerOf(id));
        const Id key = document.overrideKeyOf(id);
        NodePatch style = patch;
        if (patch.text && (document.isTextTarget(document.sourceOf(id)) ||
                          (!document.view(id)->typography.isNil() && patch.text->content == document.view(id)->text.content))) {
            if (document.isTextTarget(document.sourceOf(id)) && patch.text->content != document.view(id)->text.content) return {};
            style.text.reset();
            const auto changed = textChanges(document.view(id)->text, *patch.text);
            if (changed != TextStyle{}) style.textStyle = changed;
        }
        style.type.reset();
        style.component.reset();
        if (!instance || key.isNil() || style == NodePatch{}) return {};
        return {SetOverride{instance->id, key, mergePatch(existing(*instance, key), style)}};
    }
    const Node* node = document.find(id);
    if (node && patch.text && !node->typography.isNil() && !patch.typography) {
        if (textChanges(document.view(id)->text, *patch.text) != TextStyle{}) return {};
        NodePatch adjusted=patch; adjusted.text=node->text; adjusted.text->content=patch.text->content;
        return {SetProps{id,adjusted}};
    }
    if (!node || node->type != NodeType::Instance) {
        if (node && patch.text && document.isTextTarget(id)) {
            if (patch.text->content != document.view(id)->text.content) return {};
            NodePatch adjusted = patch; adjusted.text.reset();
            const auto changed = textChanges(document.view(id)->text, *patch.text);
            if (changed != TextStyle{}) adjusted.textStyle = changed;
            if (adjusted == NodePatch{}) return {};
            return {SetProps{id, adjusted}};
        }
        return {SetProps{id, patch}};
    }
    NodePatch own;
    own.name = patch.name;
    own.x = patch.x;
    own.y = patch.y;
    own.w = patch.w;
    own.h = patch.h;
    own.widthSizing = patch.widthSizing;
    own.heightSizing = patch.heightSizing;
    own.limits = patch.limits;
    own.absolute = patch.absolute;
    own.visible = patch.visible;
    own.locked = patch.locked;
    own.component = patch.component;
    own.componentValues = patch.componentValues;

    NodePatch style = patch;
    for (auto* field : {&style.x, &style.y, &style.w, &style.h}) field->reset();
    style.name.reset();
    style.widthSizing.reset();
    style.heightSizing.reset();
    style.limits.reset();
    style.absolute.reset();
    style.visible.reset();
    style.locked.reset();
    style.component.reset();
    style.componentValues.reset();
    style.type.reset();

    std::vector<OpBody> ops;
    if (own != NodePatch{}) ops.push_back(SetProps{id, own});
    if (style != NodePatch{}) {
        const Id root = patch.component.value_or(node->component);
        ops.push_back(SetOverride{id, root, mergePatch(existing(*node, root), style)});
    }
    return ops;
}

bool Editor::setProps(const Id& id, const NodePatch& patch) {
    if(presenting_) return false;
    if (gesture_ != Gesture::None || patch == NodePatch{}) {
        return false;
    }
    settlePropsPreview();
    const auto ops = propsOps(id, patch);
    if (ops.empty()) {
        return false;
    }
    if (ops.size() == 1) {
        return session_.edit(ops[0]);
    }
    // Geometry and appearance edits must succeed together (especially when changing a main).
    if (!session_.preview(ops)) return false;
    return session_.commitPreview();
}

bool Editor::previewProps(const Id& id, const NodePatch& patch) {
    if(presenting_) return false;
    if (gesture_ != Gesture::None || patch == NodePatch{}) {
        return false;
    }
    cancelPath();
    const auto ops = propsOps(id, patch);
    propsPreview_ = !ops.empty() && session_.preview(ops);
    return propsPreview_;
}

bool Editor::setComponentDefinition(const Id& component, const ComponentDefinition& definition) {
    if(presenting_) return false;
    if (gesture_ != Gesture::None) return false;
    settlePropsPreview();
    return session_.edit(SetComponentDefinition{component, definition});
}

std::optional<Id> Editor::importImage(const Raster& image, std::string_view name) {
    if(presenting_) return std::nullopt;
    if(!validRaster(image)) return std::nullopt;
    pointerCancel();
    cancelPropsPreview();
    Node node;
    node.id=session_.newId();
    node.type=NodeType::Image;
    node.name=name.empty()?"Image":std::string(name);
    node.image=image;
    node.absolute=true;
    node.w=image.width;
    node.h=image.height;
    const auto origin=camera_.position();
    node.x=origin.x+40;
    node.y=origin.y+40;
    if(selection_.size()==1) {
        const auto* parent=session_.document().find(selection_.front());
        if(parent && (parent->type==NodeType::Frame || parent->type==NodeType::Component)) {
            if(parent->locked) return std::nullopt;
            node.parent=parent->id;
            node.x=20;
            node.y=20;
        }
    }
    const auto& siblings=session_.document().children(node.parent);
    const auto last=siblings.empty()?"":session_.document().find(siblings.back())->order;
    node.order=keyBetween(last,std::nullopt);
    const auto id=node.id;
    if(!session_.edit(CreateNode{std::move(node)})) return std::nullopt;
    select(id);
    setTool(Tool::Select);
    return id;
}

std::optional<Id> Editor::createComponent() {
    if(presenting_) return std::nullopt;
    if (gesture_ != Gesture::None || selection_.empty()) {
        return std::nullopt;
    }
    settlePropsPreview();
    const Document& document = session_.document();
    if (selection_.size() == 1) {
        const Node* node = document.find(selection_[0]);
        if (node && node->type == NodeType::Frame) {
            NodePatch patch;
            patch.type = NodeType::Component;
            return session_.edit(SetProps{node->id, patch}) ? std::optional{node->id} : std::nullopt;
        }
    }

    const Id parent = document.find(selection_[0])->parent;
    Rect bounds = *worldBounds(document, selection_[0], activeMode_);
    for (const Id& id : selection_) {
        if (document.find(id)->parent != parent) return std::nullopt;
        const Rect r = *worldBounds(document, id, activeMode_);
        const double x1 = std::max(bounds.x + bounds.w, r.x + r.w);
        const double y1 = std::max(bounds.y + bounds.h, r.y + r.h);
        bounds.x = std::min(bounds.x, r.x);
        bounds.y = std::min(bounds.y, r.y);
        bounds.w = x1 - bounds.x;
        bounds.h = y1 - bounds.y;
    }
    const Vec2 origin = originOf(parent);
    std::string last;
    for (const Id& id : document.children(parent)) last = document.find(id)->order;

    Node component;
    component.id = session_.newId();
    component.type = NodeType::Component;
    component.parent = parent;
    component.order = keyBetween(last, std::nullopt);
    component.name = "Component";
    component.x = bounds.x - origin.x;
    component.y = bounds.y - origin.y;
    component.w = bounds.w;
    component.h = bounds.h;
    component.fill = Oklch{1.0, 0.0, 0.0};
    component.fillVisible = false;

    std::vector<Id> moved = selection_;
    std::sort(moved.begin(), moved.end(), [&](const Id& a, const Id& b) {
        return document.find(a)->order < document.find(b)->order;
    });
    session_.begin();
    bool ok = session_.edit(CreateNode{component});
    std::string order;
    for (const Id& id : moved) {
        const Rect r = *worldBounds(session_.document(), id, activeMode_);
        order = keyBetween(order, std::nullopt);
        ok = ok && session_.edit(MoveNode{id, component.id, order});
        NodePatch at;
        at.x = r.x - bounds.x;
        at.y = r.y - bounds.y;
        ok = ok && session_.edit(SetProps{id, at});
    }
    session_.commit();
    if (!ok) {
        session_.undo();
        return std::nullopt;
    }
    select(component.id);
    return component.id;
}

std::optional<Id> Editor::createInstance(const Id& componentId) {
    if(presenting_) return std::nullopt;
    const Document& document = session_.document();
    const Node* component = document.find(componentId);
    if (gesture_ != Gesture::None || !component || component->type != NodeType::Component) {
        return std::nullopt;
    }
    settlePropsPreview();
    const Rect box = document.box(componentId, activeMode_);
    std::string last;
    for (const Id& id : document.children(component->parent)) last = document.find(id)->order;

    Node instance;
    instance.id = session_.newId();
    instance.type = NodeType::Instance;
    instance.component = componentId;
    instance.parent = component->parent;
    instance.order = keyBetween(last, std::nullopt);
    instance.name = component->name;
    instance.x = box.x + box.w + 40.0;
    instance.y = box.y;
    instance.w = box.w;
    instance.h = box.h;
    instance.widthSizing = component->widthSizing;
    instance.heightSizing = component->heightSizing;
    if (!session_.edit(CreateNode{instance})) {
        return std::nullopt;
    }
    select(instance.id);
    return instance.id;
}

std::optional<Id> Editor::detachInstance(const Id& instanceId) {
    if(presenting_) return std::nullopt;
    const Document& document = session_.document();
    const Node* instance = document.find(instanceId);
    if (gesture_ != Gesture::None || !instance || instance->type != NodeType::Instance) {
        return std::nullopt;
    }
    settlePropsPreview();

    std::vector<Node> copies;
    const auto copy = [&](const Id& key, const Id& parent, auto&& self) -> void {
        Node node = *document.view(key);
        if (!node.typography.isNil()) {
            const auto value=document.resolveToken(node.typography,activeMode_);
            if (value) if (const auto* style=std::get_if<TextStyle>(&*value)) {
                Node inherited=node; NodePatch patch; patch.textStyle=*style; applyPatch(inherited,patch);
                // A detached source has no component override layer. Bake any
                // differing typography so detaching cannot change its appearance.
                if (inherited.text != node.text) node.typography={};
            }
        }
        node.id = session_.newId();
        node.parent = parent;
        node.definition = {};
        node.componentValues.clear();
        if (node.type == NodeType::Instance) {
            node.type = NodeType::Frame;
            node.component = {};
            node.overrides.clear();
        }
        const Id id = node.id;
        copies.push_back(std::move(node));
        for (const Id& child : document.viewChildren(key)) self(child, id, self);
    };
    copy(instanceId, instance->parent, copy);
    copies.front().order = instance->order;

    std::vector<OpBody> ops{DeleteNode{instanceId}};
    for (const Node& node : copies) ops.push_back(CreateNode{node});
    if (!session_.preview(ops) || !session_.commitPreview()) return std::nullopt;
    select(copies.front().id);
    return copies.front().id;
}

void Editor::reveal(const Id& id, Vec2 viewport) {
    if(presenting_) return;
    const auto bounds = worldBounds(session_.document(), id, activeMode_);
    if (!bounds) {
        return;
    }
    select(id);
    const Rect view = camera_.visibleWorld(viewport);
    if (bounds->x >= view.x && bounds->y >= view.y && bounds->x + bounds->w <= view.x + view.w &&
        bounds->y + bounds->h <= view.y + view.h) {
        return;
    }
    const Vec2 center = camera_.toScreen({bounds->x + bounds->w / 2.0, bounds->y + bounds->h / 2.0});
    camera_.pan({viewport.x / 2.0 - center.x, viewport.y / 2.0 - center.y});
}

bool Editor::resetOverrides(const Id& instanceId) {
    if(presenting_) return false;
    const Document& document = session_.document();
    const Id owner = document.ownerOf(instanceId);
    const bool copy = owner != instanceId;
    const Node* instance = document.find(copy ? owner : instanceId);
    if (gesture_ != Gesture::None || !instance || instance->type != NodeType::Instance || instance->overrides.empty()) {
        return false;
    }
    settlePropsPreview();
    const std::vector<Override> overrides = instance->overrides;
    if (copy) {
        const Id key = document.overrideKeyOf(instanceId);
        const bool changed = std::any_of(overrides.begin(), overrides.end(), [&](const Override& o) { return o.node == key; });
        return changed && session_.edit(SetOverride{owner, key, {}});
    }
    session_.begin();
    for (const Override& o : overrides) session_.edit(SetOverride{instanceId, o.node, {}});
    session_.commit();
    return true;
}

bool Editor::commitPropsPreview() {
    if(presenting_) return false;
    if (!propsPreview_) {
        return false;
    }
    propsPreview_ = false;
    return session_.commitPreview();
}

void Editor::cancelPropsPreview() {
    if(presenting_) return;
    if (propsPreview_) {
        propsPreview_ = false;
        session_.cancelPreview();
    }
}

void Editor::settlePropsPreview() {
    cancelPath();
    commitPropsPreview();
}

bool Editor::nudge(Vec2 delta) {
    if(presenting_) return false;
    if (selection_.empty() || gesture_ != Gesture::None) {
        return false;
    }
    settlePropsPreview();
    session_.begin();
    for (const Id& id : selection_) {
        const Node* found = session_.document().find(id);
        if (!found || found->locked) continue;
        const Node& node = *found;
        NodePatch patch;
        patch.x = node.x + delta.x;
        patch.y = node.y + delta.y;
        session_.edit(SetProps{id, patch});
    }
    session_.commit();
    return true;
}

void Editor::select(const Id& id) {
    if(presenting_) return;
    cancelPath();
    if (!exists(id)) {
        return;
    }
    if (selection_.size() != 1 || selection_[0] != id) {
        selection_ = {id};
        touch();
    }
}

void Editor::clearSelection() {
    if(presenting_) return;
    cancelPath();
    if (!selection_.empty()) {
        selection_.clear();
        touch();
    }
}

bool Editor::deleteSelection() {
    if(presenting_) return false;
    const bool onlyCopies = std::all_of(selection_.begin(), selection_.end(), [this](const Id& id) { return isCopy(id); });
    if (gesture_ != Gesture::None || onlyCopies) {
        return false;
    }
    settlePropsPreview();
    session_.begin();
    for (const Id& id : selection_) {
        if (!isCopy(id)) session_.deleteTree(id);
    }
    session_.commit();
    clearSelection();
    return true;
}

bool Editor::undo() {
    if(presenting_) return false;
    if (gesture_ != Gesture::None) {
        return false;
    }
    settlePropsPreview();
    const bool undone = session_.undo();
    pruneSelection();
    return undone;
}

bool Editor::redo() {
    if(presenting_) return false;
    if (gesture_ != Gesture::None) {
        return false;
    }
    settlePropsPreview();
    const bool redone = session_.redo();
    pruneSelection();
    return redone;
}

void Editor::setPixelSnap(bool enabled) {
    if(presenting_) return;
    pixelSnap_ = enabled;
}

double Editor::snap(double world) const {
    return pixelSnap_ ? std::round(world) : world;
}

Vec2 Editor::originOf(const Id& parent) const {
    const auto bounds = worldBounds(session_.document(), parent, activeMode_);
    return bounds ? Vec2{bounds->x, bounds->y} : Vec2{};
}

Vec2 Editor::pathCursor(Vec2 world) const {
    if (!penPoints_.empty() && modifiers_.shift) {
        world = penPoints_.back() + constrainedLine(world - penPoints_.back());
    }
    return {snap(world.x), snap(world.y)};
}

void Editor::addPathPoint(Vec2 world) {
    if (penPoints_.empty()) {
        clearSelection();
        target_ = session_.newId();
        drawParent_ = frameAt(session_.document(), world, activeMode_, {});
        parentOrigin_ = originOf(drawParent_);
        penClosed_ = false;
    } else {
        // Browser double clicks deliver two pointer-downs before dblclick. Keep one vertex.
        if (distance(camera_.toScreen(world), camera_.toScreen(penPoints_.back())) < kClickSlopPx) return;
        if (penPoints_.size() >= 3 &&
            distance(camera_.toScreen(world), camera_.toScreen(penPoints_.front())) <= kHandleSizePx / 2) {
            penClosed_ = true;
            finishPath();
            return;
        }
    }
    if (penPoints_.size() >= 64) return;
    const auto point=pathCursor(world);
    penPoints_.push_back(point);
    if(!penInHandles_.empty()) {
        penInHandles_.push_back(point);
        penOutHandles_.push_back(point);
    }
    gesture_ = Gesture::PenCurve;
    previewPath(false);
    touch();
}

void Editor::updatePenCurve(Vec2 world) {
    if(!drawingPath() || distance(lastScreen_,downScreen_)<kClickSlopPx) return;
    const auto anchor=penPoints_.back();
    auto extent=world-anchor;
    if(modifiers_.shift) extent=constrainedLine(extent);
    const auto outgoing=Vec2{snap(anchor.x+extent.x),snap(anchor.y+extent.y)};
    if(penInHandles_.empty()) {penInHandles_=penPoints_;penOutHandles_=penPoints_;}
    penOutHandles_.back()=outgoing;
    penInHandles_.back()=anchor-(outgoing-anchor);
    previewPath(false);
}

void Editor::previewPath(bool cursorPoint) {
    if (!drawingPath()) return;
    Path path;path.points=penPoints_;path.closed=penClosed_;
    path.inHandles=penInHandles_;path.outHandles=penOutHandles_;
    if (cursorPoint && path.points.size() < 64) {
        const auto cursor = pathCursor(camera_.toWorld(lastScreen_));
        if (distance(cursor, path.points.back()) > 0) {
            path.points.push_back(cursor);
            if(!path.inHandles.empty()) {path.inHandles.push_back(cursor);path.outHandles.push_back(cursor);}
        }
    }
    // A first point has a zero-length preview, never a committed singleton path.
    if (path.points.size() == 1) {path.points.push_back(path.points.front());path.inHandles.clear();path.outHandles.clear();}
    for (auto* points:{&path.points,&path.inHandles,&path.outHandles})
        for (auto& point : *points) point = point - parentOrigin_;
    session_.cancelPreview();
    const auto& siblings = session_.document().children(drawParent_);
    const auto last = siblings.empty() ? "" : session_.document().find(siblings.back())->order;
    Node node = newShape(Tool::Pen);
    node.id = target_;
    node.parent = drawParent_;
    node.order = keyBetween(last, std::nullopt);
    applyPatch(node, pathGeometry(path));
    session_.preview(CreateNode{std::move(node)});
}

bool Editor::finishPath() {
    if(presenting_) return false;
    if (!drawingPath()) return false;
    if (penPoints_.size() < 2) {
        cancelPath();
        return false;
    }
    previewPath(false);
    const bool committed = session_.commitPreview();
    const auto id = target_;
    penPoints_.clear();
    penInHandles_.clear();
    penOutHandles_.clear();
    penClosed_ = false;
    if(gesture_==Gesture::PenCurve) gesture_=Gesture::None;
    if (committed) select(id);
    setTool(Tool::Select);
    touch();
    return committed;
}

void Editor::cancelPath() {
    if(presenting_) return;
    if (!drawingPath()) return;
    penPoints_.clear();
    penInHandles_.clear();
    penOutHandles_.clear();
    penClosed_ = false;
    if(gesture_==Gesture::PenCurve) gesture_=Gesture::None;
    session_.cancelPreview();
    touch();
}

bool Editor::popPathPoint() {
    if(presenting_) return false;
    if (!drawingPath()) return false;
    penPoints_.pop_back();
    if(!penInHandles_.empty()) {penInHandles_.pop_back();penOutHandles_.pop_back();}
    if(gesture_==Gesture::PenCurve) gesture_=Gesture::None;
    if (penPoints_.empty()) session_.cancelPreview();
    else previewPath(true);
    touch();
    return true;
}

void Editor::updatePencil(Vec2 world) {
    const Vec2 point{snap(world.x),snap(world.y)};
    if(pencilPoints_.empty() || distance(point,pencilPoints_.back())==0) return;
    pencilPoints_.push_back(point);
    if(pencilPoints_.size()>64) {
        // Drop the least significant interior sample. Stable ties and fixed input give identical output.
        std::size_t remove=1;
        double best=std::numeric_limits<double>::infinity();
        for(std::size_t i=1;i+1<pencilPoints_.size();++i) {
            const auto a=pencilPoints_[i-1],b=pencilPoints_[i+1],p=pencilPoints_[i];
            const auto d=b-a,v=p-a;
            const double length=d.x*d.x+d.y*d.y;
            const double t=length>0?std::clamp((v.x*d.x+v.y*d.y)/length,0.0,1.0):0;
            const double dx=v.x-t*d.x,dy=v.y-t*d.y,error=dx*dx+dy*dy;
            if(error<best) {best=error;remove=i;}
        }
        pencilPoints_.erase(pencilPoints_.begin()+static_cast<std::ptrdiff_t>(remove));
    }
    previewPencil();
}

void Editor::previewPencil() {
    if(pencilPoints_.size()<2) return;
    auto points=pencilPoints_;
    for(auto& point:points) point=point-parentOrigin_;
    session_.cancelPreview();
    const auto& siblings=session_.document().children(drawParent_);
    const auto last=siblings.empty()?"":session_.document().find(siblings.back())->order;
    auto node=newShape(Tool::Pencil);node.id=target_;node.parent=drawParent_;node.order=keyBetween(last,std::nullopt);
    applyPatch(node,pathGeometry(points,false));
    session_.preview(CreateNode{std::move(node)});
}

void Editor::startDraw(Vec2 world) {
    gesture_ = Gesture::Draw;
    target_ = session_.newId();
    drawParent_ = frameAt(session_.document(), world, activeMode_, {});
    parentOrigin_ = originOf(drawParent_);
}

void Editor::updateDraw(Vec2 world, Vec2 screen) {
    if (distance(screen, downScreen_) < kClickSlopPx && !session_.hasPreview()) {
        return;
    }
    Vec2 extent = world - downWorld_;
    if (modifiers_.shift && tool_ == Tool::Line) extent=constrainedLine(extent);
    else if (modifiers_.shift) {
        const double side = std::max(std::abs(extent.x), std::abs(extent.y));
        extent = {std::copysign(side, extent.x), std::copysign(side, extent.y)};
    }
    const Vec2 from = modifiers_.alt ? downWorld_ - extent : downWorld_;
    previewDraw(from, downWorld_ + extent);
}

void Editor::previewDraw(Vec2 from, Vec2 to) {
    const auto& siblings = session_.document().children(drawParent_);
    const std::string lastOrder = siblings.empty() ? "" : session_.document().find(siblings.back())->order;

    Node node = newShape(tool_);
    node.id = target_;
    node.parent = drawParent_;
    node.order = keyBetween(lastOrder, std::nullopt);
    if (tool_ == Tool::Line) {
        applyPatch(node,lineGeometry({snap(from.x)-parentOrigin_.x,snap(from.y)-parentOrigin_.y},
                                    {snap(to.x)-parentOrigin_.x,snap(to.y)-parentOrigin_.y}));
        session_.preview(CreateNode{std::move(node)}); return;
    }
    const double x0 = snap(std::min(from.x, to.x));
    const double y0 = snap(std::min(from.y, to.y));
    const double minSize = pixelSnap_ ? 1.0 : 0.0;
    node.x = x0 - parentOrigin_.x;
    node.y = y0 - parentOrigin_.y;
    node.w = std::max(snap(std::max(from.x, to.x)) - x0, minSize);
    node.h = std::max(snap(std::max(from.y, to.y)) - y0, minSize);
    if(tool_==Tool::Polygon || tool_==Tool::Star) {
        std::vector<Vec2> points;
        const int count=tool_==Tool::Star?10:5;
        for(int i=0;i<count;++i) {
            const double angle=-3.14159265358979323846/2+2*3.14159265358979323846*i/count;
            const double radius=(tool_==Tool::Star && i%2)?0.5:1;
            points.push_back({std::cos(angle)*radius,std::sin(angle)*radius});
        }
        node.path=*pathGeometry(points,true).path;
        node.path.filled=true;
    }
    session_.preview(CreateNode{std::move(node)});
}

Id Editor::pickAt(const Id& deepest) const {
    const Document& document = session_.document();
    const Id owner = document.ownerOf(deepest);
    if (owner == deepest || modifiers_.deep) {
        return deepest;
    }
    for (const Id& selected : selection_) {
        if (!isCopy(selected) || document.ownerOf(selected) != owner) continue;
        const Id level = document.view(selected)->parent;
        for (Id id = deepest; !id.isNil(); id = document.view(id)->parent) {
            if (document.view(id)->parent == level) return id;
        }
        return deepest;
    }
    return owner;
}

void Editor::startMove(Vec2 world) {
    const Id hit = pickAt(hitTest(session_.document(), world, false, activeMode_,4/camera_.zoom()));
    if (hit.isNil()) {
        startMarquee();
        return;
    }
    if (modifiers_.shift) {
        toggleSelection(hit);
        return;
    }
    if (!isSelected(hit)) {
        select(hit);
    }
    beginMoving();
}

void Editor::beginMoving() {
    const Document& document = session_.document();
    moving_.clear();
    for (const Id& id : selection_) {
        const Node* node = document.find(id);
        if (!node || node->locked) continue;
        bool insideSelected = false;
        for (const Node* p = document.find(node->parent); p && !insideSelected; p = document.find(p->parent)) {
            insideSelected = isSelected(p->id);
        }
        if (insideSelected) continue;
        const Rect bounds = *worldBounds(document, id, activeMode_);
        moving_.push_back({id, {bounds.x, bounds.y}});
    }
    if (!moving_.empty()) {
        gesture_ = Gesture::Move;
    }
}

void Editor::updateMove(Vec2 world) {
    session_.cancelPreview();
    const Document& document = session_.document();
    Vec2 delta = world - downWorld_;
    if (modifiers_.shift) {
        if (std::abs(delta.x) >= std::abs(delta.y)) {
            delta.y = 0.0;
        } else {
            delta.x = 0.0;
        }
    }

    std::unordered_set<Id> excluded;
    for (const Moving& m : moving_) excluded.insert(m.id);
    const Id container = frameAt(document, world, activeMode_, excluded);
    const Node* target = document.find(container);
    const Vec2 origin = originOf(container);

    std::vector<OpBody> ops;
    if (target && target->layout.type != LayoutType::None) {
        const bool row = target->layout.type == LayoutType::Row;
        std::vector<const Node*> flow;
        for (const Id& id : document.children(container)) {
            const Node* child = document.find(id);
            if (!excluded.contains(id) && child->visible && !child->absolute) flow.push_back(child);
        }
        std::size_t index = 0;
        for (const Node* sibling : flow) {
            const Rect bounds = *worldBounds(document, sibling->id, activeMode_);
            const double center = row ? bounds.x + bounds.w / 2.0 : bounds.y + bounds.h / 2.0;
            if ((row ? world.x : world.y) > center) ++index;
        }
        std::string before = index > 0 ? flow[index - 1]->order : "";
        const std::optional<std::string> after =
            index < flow.size() ? std::optional<std::string>{flow[index]->order} : std::nullopt;

        bool changed = false;
        for (const Moving& m : moving_) {
            const Node& node = *document.find(m.id);
            if (node.absolute && node.parent == container) {
                NodePatch patch;
                patch.x = snap(m.start.x + delta.x) - origin.x;
                patch.y = snap(m.start.y + delta.y) - origin.y;
                ops.push_back(SetProps{m.id, patch});
                changed = true;
                continue;
            }
            const std::string key = keyBetween(before, after ? std::optional<std::string_view>{*after} : std::nullopt);
            before = key;
            ops.push_back(MoveNode{m.id, container, key});
            changed = changed || node.parent != container;
        }
        if (!changed && moving_.size() == 1) {
            std::size_t current = 0;
            for (const Id& id : document.children(container)) {
                if (id == moving_[0].id) break;
                const Node* child = document.find(id);
                if (child->visible && !child->absolute) ++current;
            }
            if (current == index) ops.clear();
        }
    } else {
        const auto& siblings = document.children(container);
        std::string last;
        for (const Id& id : siblings) {
            if (!excluded.contains(id)) last = document.find(id)->order;
        }
        for (const Moving& m : moving_) {
            const Node& node = *document.find(m.id);
            if (node.parent != container) {
                last = keyBetween(last, std::nullopt);
                ops.push_back(MoveNode{m.id, container, last});
            }
            NodePatch patch;
            patch.x = snap(m.start.x + delta.x) - origin.x;
            patch.y = snap(m.start.y + delta.y) - origin.y;
            ops.push_back(SetProps{m.id, patch});
        }
    }

    if (ops.empty()) {
        session_.cancelPreview();
    } else {
        session_.preview(ops);
    }
}

void Editor::startMarquee() {
    gesture_ = Gesture::Marquee;
    marqueeBase_ = modifiers_.shift ? selection_ : std::vector<Id>{};
    if (!modifiers_.shift) {
        clearSelection();
    }
}

void Editor::updateMarquee(Vec2 world) {
    const Rect area{std::min(downWorld_.x, world.x), std::min(downWorld_.y, world.y), std::abs(world.x - downWorld_.x),
                    std::abs(world.y - downWorld_.y)};
    marquee_ = area;
    std::vector<Id> picked = marqueeBase_;
    const Document& document = session_.document();
    for (const Id& id : document.children(Id{})) {
        const Node& node = *document.find(id);
        if (!node.visible || node.locked) continue;
        auto bounds=*worldBounds(document,id,activeMode_);
        if (node.type == NodeType::Line || node.type == NodeType::Path) {const double r=node.lineWidth/2; bounds={bounds.x-r,bounds.y-r,bounds.w+2*r,bounds.h+2*r};}
        if (bounds.intersects(area) &&
            std::find(picked.begin(), picked.end(), id) == picked.end()) {
            picked.push_back(id);
        }
    }
    setSelection(std::move(picked));
    touch();
}

bool Editor::isSelected(const Id& id) const {
    return std::find(selection_.begin(), selection_.end(), id) != selection_.end();
}

void Editor::toggleSelection(const Id& id) {
    if(presenting_) return;
    cancelPath();
    if (!exists(id)) {
        return;
    }
    if (isSelected(id)) {
        std::erase(selection_, id);
    } else {
        selection_.push_back(id);
    }
    touch();
}

void Editor::setSelection(std::vector<Id> ids) {
    if(presenting_) return;
    cancelPath();
    std::erase_if(ids, [this](const Id& id) { return !exists(id); });
    if (ids != selection_) {
        selection_ = std::move(ids);
        touch();
    }
}

void Editor::setFrameLabelSizes(std::unordered_map<Id, Vec2> sizes) {
    if(presenting_) return;
    labelSizes_ = std::move(sizes);
}

Id Editor::frameLabelAt(Vec2 screen) const {
    if(presenting_) return {};
    const Document& document = session_.document();
    for (const auto& [id, size] : labelSizes_) {
        const Node* node = document.find(id);
        if (!node || !node->visible || node->locked || !node->parent.isNil()) continue;
        const Rect box = document.box(id, activeMode_);
        const Vec2 corner = camera_.toScreen({box.x, box.y});
        const Rect label{corner.x, corner.y - kLabelGapPx - size.y, std::min(size.x, box.w * camera_.zoom()), size.y};
        if (label.contains(screen)) return id;
    }
    return {};
}

void Editor::finish() {
    const Gesture gesture = std::exchange(gesture_, Gesture::None);
    switch (gesture) {
    case Gesture::PenCurve:
        break;
    case Gesture::Pencil:
        if(pencilPoints_.size()>=2) {
            previewPencil();
            if(session_.commitPreview()) select(target_);
        } else session_.cancelPreview();
        pencilPoints_.clear();
        setTool(Tool::Select);
        break;
    case Gesture::Pan:
        viewport_.pointerUp();
        break;
    case Gesture::Draw:
        if (!session_.hasPreview()) {
            previewDraw(downWorld_, {downWorld_.x + kDefaultShapeSize, downWorld_.y + (tool_==Tool::Line?0:kDefaultShapeSize)});
        }
        if (session_.commitPreview()) {
            select(target_);
        }
        setTool(Tool::Select);
        break;
    case Gesture::Move:
    case Gesture::Resize:
        session_.commitPreview();
        break;
    case Gesture::Marquee:
        marquee_.reset();
        touch();
        break;
    case Gesture::None:
        break;
    }
}

std::optional<Rect> Editor::handleBounds() const {
    if(presenting_) return std::nullopt;
    if (selection_.size() != 1) {
        return std::nullopt;
    }
    const Node* node = session_.document().find(selection_[0]);
    if (!node || node->locked) {
        return std::nullopt;
    }
    if ((node->type==NodeType::Line || node->type==NodeType::Path) && !node->absolute) {
        const auto* parent=session_.document().view(node->parent);
        if(parent && parent->layout.type!=LayoutType::None) return std::nullopt;
    }
    return worldBounds(session_.document(), selection_[0], activeMode_);
}

Handle Editor::handleAt(Vec2 screen) const {
    const auto bounds = handleBounds();
    if (tool_ != Tool::Select || !bounds) {
        return {};
    }
    const Node* selected=session_.document().view(selection_[0]);
    if (selected && selected->type==NodeType::Path) {
        const auto path=pathWorldGeometry(*selected,*bounds);
        for(std::size_t i=0;i<path.points.size();++i) {
            if(distance(camera_.toScreen(path.points[i]),screen)<=kHandleSizePx/2) return {0,0,static_cast<int>(i),0};
        }
        if(!path.inHandles.empty()) for(std::size_t i=0;i<path.points.size();++i) {
            if(distance(camera_.toScreen(path.inHandles[i]),screen)<=kHandleSizePx/2) return {0,0,static_cast<int>(i),1};
            if(distance(camera_.toScreen(path.outHandles[i]),screen)<=kHandleSizePx/2) return {0,0,static_cast<int>(i),2};
        }
        return {};
    }
    if (selected && selected->type==NodeType::Line) {
        const auto points=lineEndpoints(*selected,*bounds);
        for(std::size_t i=0;i<2;++i) {
            if(distance(camera_.toScreen(points[i]),screen)<=kHandleSizePx/2) return {i==0?-1:1,i==0?(selected->lineReverse?1:-1):(selected->lineReverse?-1:1)};
        }
        return {};
    }
    const Vec2 topLeft = camera_.toScreen({bounds->x, bounds->y});
    const Vec2 bottomRight = camera_.toScreen({bounds->x + bounds->w, bounds->y + bounds->h});
    const auto near = [](double value, double edge, double reach) { return std::abs(value - edge) <= reach; };

    for (int x : {-1, 1}) {
        for (int y : {-1, 1}) {
            const double cornerX = x < 0 ? topLeft.x : bottomRight.x;
            const double cornerY = y < 0 ? topLeft.y : bottomRight.y;
            if (near(screen.x, cornerX, kHandleSizePx / 2) && near(screen.y, cornerY, kHandleSizePx / 2)) {
                return {x, y};
            }
        }
    }
    const bool withinX = screen.x >= topLeft.x && screen.x <= bottomRight.x;
    const bool withinY = screen.y >= topLeft.y && screen.y <= bottomRight.y;
    if (withinY && near(screen.x, topLeft.x, kEdgeGrabPx)) return {-1, 0};
    if (withinY && near(screen.x, bottomRight.x, kEdgeGrabPx)) return {1, 0};
    if (withinX && near(screen.y, topLeft.y, kEdgeGrabPx)) return {0, -1};
    if (withinX && near(screen.y, bottomRight.y, kEdgeGrabPx)) return {0, 1};
    return {};
}

std::string_view Editor::cursor() const {
    if(presenting_) {
        return prototypeHotspot(lastScreen_).isNil()?"default":"pointer";
    }
    const auto resizeCursor = [](Handle h) -> std::string_view {
        if (h.point >= 0) return "move";
        if (h.x * h.y > 0) return "nwse-resize";
        if (h.x * h.y < 0) return "nesw-resize";
        if (h.x != 0) return "ew-resize";
        if (h.y != 0) return "ns-resize";
        return "default";
    };
    switch (gesture_) {
    case Gesture::PenCurve:
    case Gesture::Pencil:
        return "crosshair";
    case Gesture::Pan:
        return "grabbing";
    case Gesture::Draw:
        return "crosshair";
    case Gesture::Resize:
        return resizeCursor(handle_);
    case Gesture::Move:
    case Gesture::Marquee:
        return "default";
    case Gesture::None:
        break;
    }
    if (tool_ == Tool::Hand) return "grab";
    if (isDrawTool(tool_) || tool_ == Tool::Pen || tool_ == Tool::Pencil) return "crosshair";
    return resizeCursor(handleAt(lastScreen_));
}

void Editor::startResize(Handle handle) {
    gesture_ = Gesture::Resize;
    handle_ = handle;
    target_ = selection_[0];
    resizeStart_ = *worldBounds(session_.document(), target_, activeMode_);
    resizeLineReverse_=session_.document().find(target_)->lineReverse;
    const auto* node=session_.document().find(target_);
    resizePath_ = node->type==NodeType::Path ? pathWorldGeometry(*node,resizeStart_) : Path{};
    parentOrigin_ = originOf(session_.document().find(target_)->parent);
}

void Editor::updateResize(Vec2 world) {
    const Vec2 delta = world - downWorld_;
    const Rect& start = resizeStart_;
    if (handle_.point >= 0) {
        auto path=resizePath_;
        const auto index=static_cast<std::size_t>(handle_.point);
        if (index>=path.points.size()) return;
        if(handle_.control==0) {
            auto moving=path.points[index]+delta;
            if(modifiers_.shift && path.points.size()>1) {
                const auto anchor=path.points[index==0 ? 1 : index-1];
                moving=anchor+constrainedLine(moving-anchor);
            }
            moving={snap(moving.x),snap(moving.y)};
            const auto displacement=moving-path.points[index];
            path.points[index]=moving;
            if(!path.inHandles.empty()) {
                path.inHandles[index]=path.inHandles[index]+displacement;
                path.outHandles[index]=path.outHandles[index]+displacement;
            }
        } else {
            if(path.inHandles.empty()) return;
            auto& controls=handle_.control==1?path.inHandles:path.outHandles;
            auto& opposite=handle_.control==1?path.outHandles:path.inHandles;
            const auto anchor=path.points[index];
            auto moving=controls[index]+delta;
            if(modifiers_.shift) moving=anchor+constrainedLine(moving-anchor);
            moving={snap(moving.x),snap(moving.y)};
            controls[index]=moving;
            if(!modifiers_.alt) opposite[index]=anchor-(moving-anchor);
        }
        for(auto* points:{&path.points,&path.inHandles,&path.outHandles})
            for(auto& point:*points) point=point-parentOrigin_;
        session_.preview(SetProps{target_,pathGeometry(path)});
        return;
    }
    if (session_.document().find(target_)->type==NodeType::Line) {
        Node initial; initial.lineReverse=resizeLineReverse_;
        auto points=lineEndpoints(initial,start);
        const int moving=handle_.x<0?0:1, fixed=1-moving;
        const auto center=Vec2{(points[0].x+points[1].x)/2,(points[0].y+points[1].y)/2};
        const auto anchor=modifiers_.alt?center:points[fixed];
        auto extent=points[moving]+delta-anchor;
        if(modifiers_.shift) extent=constrainedLine(extent);
        points[moving]=anchor+extent;
        if(modifiers_.alt) points[fixed]=anchor-extent;
        for(auto& p:points) p={snap(p.x)-parentOrigin_.x,snap(p.y)-parentOrigin_.y};
        session_.preview(SetProps{target_,lineGeometry(points[0],points[1])}); return;
    }
    const double startX1 = start.x + start.w;
    const double startY1 = start.y + start.h;
    double x0 = start.x;
    double x1 = startX1;
    double y0 = start.y;
    double y1 = startY1;

    const auto moveEdges = [this](int side, double d, double& low, double& high) {
        if (side < 0) {
            low += d;
            if (modifiers_.alt) high -= d;
        } else if (side > 0) {
            high += d;
            if (modifiers_.alt) low -= d;
        }
    };
    moveEdges(handle_.x, delta.x, x0, x1);
    moveEdges(handle_.y, delta.y, y0, y1);

    if (modifiers_.shift && start.w > 0.0 && start.h > 0.0) {
        double w = x1 - x0;
        double h = y1 - y0;
        if (handle_.x != 0 && handle_.y != 0) {
            const double scale = std::max(std::abs(w) / start.w, std::abs(h) / start.h);
            w = std::copysign(scale * start.w, w);
            h = std::copysign(scale * start.h, h);
        } else if (handle_.x != 0) {
            h = std::abs(w) / start.w * start.h;
        } else {
            w = std::abs(h) / start.h * start.w;
        }
        const auto place = [this](int side, double size, double startLow, double startHigh, double& low,
                                  double& high) {
            if (side == 0 || modifiers_.alt) {
                const double center = (startLow + startHigh) / 2;
                low = center - size / 2;
                high = center + size / 2;
            } else if (side > 0) {
                high = startLow + size;
            } else {
                low = startHigh - size;
            }
        };
        place(handle_.x, w, start.x, startX1, x0, x1);
        place(handle_.y, h, start.y, startY1, y0, y1);
    }

    const auto snapMoved = [this](double value, double original) {
        return value == original ? value : snap(value);
    };
    x0 = snapMoved(x0, start.x);
    x1 = snapMoved(x1, startX1);
    y0 = snapMoved(y0, start.y);
    y1 = snapMoved(y1, startY1);

    const double minSize = pixelSnap_ ? 1.0 : 0.0;
    NodePatch patch;
    patch.x = std::min(x0, x1) - parentOrigin_.x;
    patch.y = std::min(y0, y1) - parentOrigin_.y;
    patch.w = std::max(std::abs(x1 - x0), minSize);
    patch.h = std::max(std::abs(y1 - y0), minSize);
    const Node& node = *session_.document().find(target_);
    const bool widthMoved = handle_.x != 0 || (modifiers_.shift && handle_.y != 0);
    const bool heightMoved = handle_.y != 0 || (modifiers_.shift && handle_.x != 0);
    if (widthMoved && node.widthSizing != Sizing::Fixed) patch.widthSizing = Sizing::Fixed;
    if (heightMoved && node.heightSizing != Sizing::Fixed) patch.heightSizing = Sizing::Fixed;
    session_.preview(SetProps{target_, patch});
}

void Editor::pruneSelection() {
    const auto removed = std::erase_if(selection_, [this](const Id& id) { return !exists(id); });
    if (removed > 0) {
        touch();
    }
}

bool Editor::exists(const Id& id) const {
    return session_.document().view(id) != nullptr;
}

bool Editor::isCopy(const Id& id) const {
    const Id owner = session_.document().ownerOf(id);
    return !owner.isNil() && owner != id;
}

} // namespace vc
