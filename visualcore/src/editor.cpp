#include "vc/editor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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
    if (gesture_ == Gesture::Draw || gesture_ == Gesture::Move) {
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

bool Editor::nudge(Vec2 delta) {
    if (selection_.empty() || gesture_ != Gesture::None) {
        return false;
    }
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
    const bool undone = session_.undo();
    pruneSelection();
    return undone;
}

bool Editor::redo() {
    if (gesture_ != Gesture::None) {
        return false;
    }
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
        session_.commitPreview();
        break;
    case Gesture::None:
        break;
    }
}

void Editor::pruneSelection() {
    const auto removed = std::erase_if(selection_, [this](const Id& id) { return !session_.document().find(id); });
    if (removed > 0) {
        touch();
    }
}

} // namespace vc
