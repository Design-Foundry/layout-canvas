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
    if (gesture_ != Gesture::None || tool_ != Tool::Select || selection_.size() != 1) {
        return;
    }
    const Document& document = session_.document();
    for (Id id = hitTest(document, camera_.toWorld(screen), false, activeMode_); !id.isNil();
         id = document.view(id)->parent) {
        if (document.view(id)->parent == selection_[0]) {
            select(id);
            return;
        }
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
    case Gesture::Marquee:
        updateMarquee(world);
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
    marquee_.reset();
    gesture_ = Gesture::None;
}

void Editor::wheel(Vec2 screen, double deltaY) {
    viewport_.wheel(screen, deltaY);
}

std::vector<OpBody> Editor::propsOps(const Id& id, const NodePatch& patch) const {
    const Document& document = session_.document();
    const auto existing = [](const Node& instance, const Id& key) {
        for (const Override& o : instance.overrides) {
            if (o.node == key) return o.patch;
        }
        return NodePatch{};
    };
    if (isCopy(id)) {
        const Node* instance = document.find(document.ownerOf(id));
        const Id key = document.overrideKeyOf(id);
        NodePatch style = patch;
        style.type.reset();
        style.component.reset();
        if (!instance || key.isNil() || style == NodePatch{}) return {};
        return {SetOverride{instance->id, key, mergePatch(existing(*instance, key), style)}};
    }
    const Node* node = document.find(id);
    if (!node || node->type != NodeType::Instance) {
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
    style.type.reset();

    std::vector<OpBody> ops;
    if (own != NodePatch{}) ops.push_back(SetProps{id, own});
    if (style != NodePatch{}) {
        ops.push_back(SetOverride{id, node->component, mergePatch(existing(*node, node->component), style)});
    }
    return ops;
}

bool Editor::setProps(const Id& id, const NodePatch& patch) {
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
    session_.begin();
    bool applied = true;
    for (const OpBody& op : ops) applied = session_.edit(op) && applied;
    session_.commit();
    return applied;
}

bool Editor::previewProps(const Id& id, const NodePatch& patch) {
    if (gesture_ != Gesture::None || patch == NodePatch{}) {
        return false;
    }
    const auto ops = propsOps(id, patch);
    propsPreview_ = !ops.empty() && session_.preview(ops);
    return propsPreview_;
}

std::optional<Id> Editor::createComponent() {
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
    const Document& document = session_.document();
    const Node* instance = document.find(instanceId);
    if (gesture_ != Gesture::None || !instance || instance->type != NodeType::Instance) {
        return std::nullopt;
    }
    settlePropsPreview();

    std::vector<Node> copies;
    const auto copy = [&](const Id& key, const Id& parent, auto&& self) -> void {
        Node node = *document.view(key);
        node.id = session_.newId();
        node.parent = parent;
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

    session_.begin();
    bool ok = session_.edit(DeleteNode{instanceId});
    for (const Node& node : copies) ok = ok && session_.edit(CreateNode{node});
    session_.commit();
    if (!ok) {
        session_.undo();
        return std::nullopt;
    }
    select(copies.front().id);
    return copies.front().id;
}

void Editor::reveal(const Id& id, Vec2 viewport) {
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
    const Node* instance = session_.document().find(instanceId);
    if (gesture_ != Gesture::None || !instance || instance->type != NodeType::Instance || instance->overrides.empty()) {
        return false;
    }
    settlePropsPreview();
    const std::vector<Override> overrides = instance->overrides;
    session_.begin();
    for (const Override& o : overrides) session_.edit(SetOverride{instanceId, o.node, {}});
    session_.commit();
    return true;
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
    if (!exists(id)) {
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
    const auto bounds = worldBounds(session_.document(), parent, activeMode_);
    return bounds ? Vec2{bounds->x, bounds->y} : Vec2{};
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
    const Id hit = pickAt(hitTest(session_.document(), world, false, activeMode_));
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
        if (worldBounds(document, id, activeMode_)->intersects(area) &&
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
    std::erase_if(ids, [this](const Id& id) { return !exists(id); });
    if (ids != selection_) {
        selection_ = std::move(ids);
        touch();
    }
}

void Editor::setFrameLabelSizes(std::unordered_map<Id, Vec2> sizes) {
    labelSizes_ = std::move(sizes);
}

Id Editor::frameLabelAt(Vec2 screen) const {
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
    case Gesture::Marquee:
        marquee_.reset();
        touch();
        break;
    case Gesture::None:
        break;
    }
}

std::optional<Rect> Editor::handleBounds() const {
    if (selection_.size() != 1) {
        return std::nullopt;
    }
    const Node* node = session_.document().find(selection_[0]);
    if (!node || node->locked) {
        return std::nullopt;
    }
    return worldBounds(session_.document(), selection_[0], activeMode_);
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
    case Gesture::Marquee:
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
    resizeStart_ = *worldBounds(session_.document(), target_, activeMode_);
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
