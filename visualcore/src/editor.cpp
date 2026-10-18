#include "vc/editor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <unordered_set>
#include <utility>
#include "vc/fractional_index.hpp"
#include "vc/geometry.hpp"

namespace vc {
namespace {

constexpr std::array<std::pair<Tool, std::string_view>, 5> kToolNames{{
    {Tool::Select, "select"},
    {Tool::Frame, "frame"},
    {Tool::Rect, "rectangle"},
    {Tool::Ellipse, "ellipse"},
    {Tool::Hand, "hand"},
}};

bool isDrawTool(Tool tool) {
    return tool == Tool::Frame || tool == Tool::Rect || tool == Tool::Ellipse;
}

double distance(Vec2 a, Vec2 b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}

Node newShape(Tool tool) {
    Node node;
    switch (tool) {
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

bool Editor::edit(const OpBody& op) {
    if (gesture_ != Gesture::None) {
        return false;
    }
    settlePropsPreview();
    return session_.edit(op);
}

std::optional<Id> Editor::addToken(TokenType type) {
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
    }
    const Id id = token.id;
    return edit(CreateToken{std::move(token)}) ? std::optional{id} : std::nullopt;
}

std::optional<Id> Editor::addMode() {
    const auto modes = session_.document().modes();
    Mode mode{session_.newId(), "Mode " + std::to_string(modes.size() + 1), keyBetween(modes.back()->order, std::nullopt)};
    const Id id = mode.id;
    return edit(CreateMode{std::move(mode)}) ? std::optional{id} : std::nullopt;
}

bool Editor::deleteMode(const Id& mode) {
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
    if (mode != activeMode_ && session_.document().findMode(mode)) {
        activeMode_ = mode;
        touch();
    }
}

void Editor::setTool(Tool tool) {
    if (tool != tool_) {
        tool_ = tool;
        touch();
    }
}

void Editor::pointerDown(Vec2 screen, PointerButton button, Modifiers modifiers) {
    if (gesture_ != Gesture::None) {
        return;
    }
    settlePropsPreview();
    modifiers_ = modifiers;
    lastScreen_ = screen;
    downScreen_ = screen;
    downWorld_ = camera_.toWorld(screen);

    if (button == PointerButton::Middle || (button == PointerButton::Primary && tool_ == Tool::Hand)) {
        gesture_ = Gesture::Pan;
        viewport_.pointerDown(screen);
    } else if (button != PointerButton::Primary) {
        return;
    } else if (isDrawTool(tool_)) {
        startDraw(downWorld_);
    } else if (const Handle handle = handleAt(screen); !handle.isNone()) {
        startResize(handle);
    } else {
        startMove(downWorld_);
    }
}

void Editor::pointerMove(Vec2 screen, Modifiers modifiers) {
    modifiers_ = modifiers;
    lastScreen_ = screen;
    update();
}

void Editor::setModifiers(Modifiers modifiers) {
    if (modifiers.shift == modifiers_.shift && modifiers.alt == modifiers_.alt) {
        return;
    }
    modifiers_ = modifiers;
    if (gesture_ == Gesture::Draw || gesture_ == Gesture::Move || gesture_ == Gesture::Resize) {
        update();
    }
}

void Editor::update() {
    const Vec2 screen = lastScreen_;
    const Vec2 world = camera_.toWorld(screen);
    switch (gesture_) {
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
    case Gesture::None:
        break;
    }
}

void Editor::pointerUp() {
    finish();
}

void Editor::pointerCancel() {
    if (gesture_ == Gesture::Pan) {
        viewport_.pointerUp();
    }
    session_.cancelPreview();
    gesture_ = Gesture::None;
}

void Editor::wheel(Vec2 screen, double deltaY) {
    viewport_.wheel(screen, deltaY);
}

bool Editor::setProps(const Id& id, const NodePatch& patch) {
    if (gesture_ != Gesture::None || patch == NodePatch{}) {
        return false;
    }
    settlePropsPreview();
    return session_.edit(SetProps{id, patch});
}

bool Editor::previewProps(const Id& id, const NodePatch& patch) {
    if (gesture_ != Gesture::None || patch == NodePatch{}) {
        return false;
    }
    propsPreview_ = session_.preview(SetProps{id, patch});
    return propsPreview_;
}

bool Editor::commitPropsPreview() {
    if (!propsPreview_) {
        return false;
    }
    propsPreview_ = false;
    return session_.commitPreview();
}

void Editor::cancelPropsPreview() {
    if (propsPreview_) {
        propsPreview_ = false;
        session_.cancelPreview();
    }
}

void Editor::settlePropsPreview() {
    commitPropsPreview();
}

bool Editor::nudge(Vec2 delta) {
    if (selection_.empty() || gesture_ != Gesture::None) {
        return false;
    }
    settlePropsPreview();
    session_.begin();
    for (const Id& id : selection_) {
        const Node& node = *session_.document().find(id);
        NodePatch patch;
        patch.x = node.x + delta.x;
        patch.y = node.y + delta.y;
        session_.edit(SetProps{id, patch});
    }
    session_.commit();
    return true;
}

void Editor::select(const Id& id) {
    if (!session_.document().find(id)) {
        return;
    }
    if (selection_.size() != 1 || selection_[0] != id) {
        selection_ = {id};
        touch();
    }
}

void Editor::clearSelection() {
    if (!selection_.empty()) {
        selection_.clear();
        touch();
    }
}

bool Editor::deleteSelection() {
    if (selection_.empty() || gesture_ != Gesture::None) {
        return false;
    }
    settlePropsPreview();
    session_.begin();
    for (const Id& id : selection_) {
        session_.deleteTree(id);
    }
    session_.commit();
    clearSelection();
    return true;
}

bool Editor::undo() {
    if (gesture_ != Gesture::None) {
        return false;
    }
    settlePropsPreview();
    const bool undone = session_.undo();
    pruneSelection();
    return undone;
}

bool Editor::redo() {
    if (gesture_ != Gesture::None) {
        return false;
    }
    settlePropsPreview();
    const bool redone = session_.redo();
    pruneSelection();
    return redone;
}

void Editor::setPixelSnap(bool enabled) {
    pixelSnap_ = enabled;
}

double Editor::snap(double world) const {
    return pixelSnap_ ? std::round(world) : world;
}

Vec2 Editor::originOf(const Id& parent) const {
    const auto bounds = worldBounds(session_.document(), parent);
    return bounds ? Vec2{bounds->x, bounds->y} : Vec2{};
}

void Editor::startDraw(Vec2 world) {
    gesture_ = Gesture::Draw;
    target_ = session_.newId();
    drawParent_ = hitTest(session_.document(), world, true);
    parentOrigin_ = originOf(drawParent_);
}

void Editor::updateDraw(Vec2 world, Vec2 screen) {
    if (distance(screen, downScreen_) < kClickSlopPx && !session_.hasPreview()) {
        return;
    }
    Vec2 extent = world - downWorld_;
    if (modifiers_.shift) {
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
    const double x0 = snap(std::min(from.x, to.x));
    const double y0 = snap(std::min(from.y, to.y));
    const double minSize = pixelSnap_ ? 1.0 : 0.0;
    node.x = x0 - parentOrigin_.x;
    node.y = y0 - parentOrigin_.y;
    node.w = std::max(snap(std::max(from.x, to.x)) - x0, minSize);
    node.h = std::max(snap(std::max(from.y, to.y)) - y0, minSize);
    session_.preview(CreateNode{std::move(node)});
}

void Editor::startMove(Vec2 world) {
    const Id hit = hitTest(session_.document(), world);
    if (hit.isNil()) {
        clearSelection();
        return;
    }
    select(hit);
    gesture_ = Gesture::Move;
    target_ = hit;
    parentOrigin_ = originOf(session_.document().find(hit)->parent);
    const Rect bounds = *worldBounds(session_.document(), hit);
    targetStart_ = {bounds.x, bounds.y};
}

void Editor::updateMove(Vec2 world) {
    Vec2 delta = world - downWorld_;
    if (modifiers_.shift) {
        if (std::abs(delta.x) >= std::abs(delta.y)) {
            delta.y = 0.0;
        } else {
            delta.x = 0.0;
        }
    }
    NodePatch patch;
    patch.x = snap(targetStart_.x + delta.x) - parentOrigin_.x;
    patch.y = snap(targetStart_.y + delta.y) - parentOrigin_.y;
    session_.preview(SetProps{target_, patch});
}

void Editor::finish() {
    const Gesture gesture = std::exchange(gesture_, Gesture::None);
    switch (gesture) {
    case Gesture::Pan:
        viewport_.pointerUp();
        break;
    case Gesture::Draw:
        if (!session_.hasPreview()) {
            previewDraw(downWorld_, {downWorld_.x + kDefaultShapeSize, downWorld_.y + kDefaultShapeSize});
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
    case Gesture::None:
        break;
    }
}

std::optional<Rect> Editor::handleBounds() const {
    if (selection_.size() != 1) {
        return std::nullopt;
    }
    return worldBounds(session_.document(), selection_[0]);
}

Handle Editor::handleAt(Vec2 screen) const {
    const auto bounds = handleBounds();
    if (tool_ != Tool::Select || !bounds) {
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
    const auto resizeCursor = [](Handle h) -> std::string_view {
        if (h.x * h.y > 0) return "nwse-resize";
        if (h.x * h.y < 0) return "nesw-resize";
        if (h.x != 0) return "ew-resize";
        if (h.y != 0) return "ns-resize";
        return "default";
    };
    switch (gesture_) {
    case Gesture::Pan:
        return "grabbing";
    case Gesture::Draw:
        return "crosshair";
    case Gesture::Resize:
        return resizeCursor(handle_);
    case Gesture::Move:
        return "default";
    case Gesture::None:
        break;
    }
    if (tool_ == Tool::Hand) return "grab";
    if (isDrawTool(tool_)) return "crosshair";
    return resizeCursor(handleAt(lastScreen_));
}

void Editor::startResize(Handle handle) {
    gesture_ = Gesture::Resize;
    handle_ = handle;
    target_ = selection_[0];
    resizeStart_ = *worldBounds(session_.document(), target_);
    parentOrigin_ = originOf(session_.document().find(target_)->parent);
}

void Editor::updateResize(Vec2 world) {
    const Vec2 delta = world - downWorld_;
    const Rect& start = resizeStart_;
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
    session_.preview(SetProps{target_, patch});
}

void Editor::pruneSelection() {
    const auto removed = std::erase_if(selection_, [this](const Id& id) { return !session_.document().find(id); });
    if (removed > 0) {
        touch();
    }
}

} // namespace vc
