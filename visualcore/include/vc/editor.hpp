#pragma once
#include <cstdint>
#include <unordered_map>
#include <optional>
#include <string_view>
#include <vector>
#include "vc/math.hpp"
#include "vc/camera.hpp"
#include "vc/session.hpp"
#include "vc/viewport_controller.hpp"

namespace vc {

enum class Tool { Select, Frame, Rect, Ellipse, Hand, Text, Line, Pen, Pencil, Polygon, Star };

enum class PointerButton { Primary = 0, Middle = 1, Secondary = 2 };

struct Modifiers {
    /// Square shapes while drawing, one axis while moving, keep proportions while resizing.
    bool shift = false;
    /// Draw and resize from the center.
    bool alt = false;
    /// Select the deepest node under the pointer, inside instances too.
    bool deep = false;
};

/// A scoped screen translated in world units before presentation. Layers are back to front.
struct PrototypeLayer {
    Id screen;
    Vec2 offset;
    double opacity = 1;
    bool overlay = false;
};

/// A resize handle: which vertical edge (-1 left, 1 right, 0 none) and which horizontal edge
/// (-1 top, 1 bottom, 0 none) it moves. Corners move both.
struct Handle {
    int x = 0;
    int y = 0;
    int point = -1;
    /// 0 anchor, 1 incoming control, 2 outgoing control.
    int control = 0;

    [[nodiscard]] bool isNone() const { return x == 0 && y == 0 && point < 0; }
    friend bool operator==(const Handle&, const Handle&) = default;
};

/// Turns pointer input into camera moves and document edits for the active tool.
/// Screen positions are CSS pixels relative to the canvas.
class Editor {
public:
    Editor(Session& session, Camera& camera);

    void setTool(Tool tool);
    [[nodiscard]] Tool tool() const { return tool_; }
    /// Presentation is ephemeral: no document operations, selection changes or undo entries.
    bool startPrototype(const Id& screen, Vec2 viewport);
    void stopPrototype();
    bool prototypeBack();
    bool restartPrototype();
    void resizePrototype(Vec2 viewport);
    void advancePrototype(double deltaMs);
    bool closePrototypeOverlay();
    [[nodiscard]] std::vector<PrototypeLayer> prototypeLayers() const;
    [[nodiscard]] Id prototypeOverlay() const;
    [[nodiscard]] bool prototypeTransitioning() const { return prototypeDuration_>0; }
    [[nodiscard]] bool presenting() const { return presenting_; }
    [[nodiscard]] Id prototypeScreen() const { return prototypeScreen_; }
    [[nodiscard]] Id prototypeStart() const { return prototypeStart_; }
    [[nodiscard]] bool canPrototypeBack() const { return presenting_ && (!prototypeHistory_.empty() || !prototypeOverlays_.empty()); }
    [[nodiscard]] bool prototypeEligible(const Id& id) const;

    void pointerDown(Vec2 screen, PointerButton button, Modifiers modifiers = {});
    void pointerMove(Vec2 screen, Modifiers modifiers = {});
    /// Modifier keys changed without the pointer moving. Updates the gesture in progress.
    void setModifiers(Modifiers modifiers);
    void pointerUp();
    /// Selects the child of the selected node under the pointer, which is how nodes inside an
    /// instance are reached.
    void doubleClick(Vec2 screen);
    /// Ends the gesture without applying it, for example when the pointer leaves the canvas.
    void pointerCancel();
    /// Pen points remain a preview until explicitly finished. Leaving the canvas never commits.
    bool finishPath();
    void cancelPath();
    bool popPathPoint();
    [[nodiscard]] bool drawingPath() const { return !penPoints_.empty(); }
    void wheel(Vec2 screen, double deltaY);
    /// Applies `patch` to `id` as one undo step, for edits made outside the canvas.
    bool setProps(const Id& id, const NodePatch& patch);
    bool setComponentDefinition(const Id& component, const ComponentDefinition& definition);
    /// Imports validated local RGBA pixels as one undo step, into a selected source frame or canvas.
    std::optional<Id> importImage(const Raster& image, std::string_view name);
    /// Shows `patch` on `id` without recording it, replacing the previous props preview. For live
    /// controls such as a color picker; finish with `commitPropsPreview` or `cancelPropsPreview`.
    /// Anything else that edits the document commits a pending preview first.
    bool previewProps(const Id& id, const NodePatch& patch);
    bool commitPropsPreview();
    void cancelPropsPreview();
    /// Moves the selected nodes by `delta` world units, as one undo step.
    bool nudge(Vec2 delta);

    [[nodiscard]] const std::vector<Id>& selection() const { return selection_; }
    [[nodiscard]] bool isSelected(const Id& id) const;
    void select(const Id& id);
    /// Adds `id` to the selection, or removes it if it was there.
    void toggleSelection(const Id& id);
    void setSelection(std::vector<Id> ids);
    /// The selection rectangle being dragged, in world space.
    [[nodiscard]] const std::optional<Rect>& marquee() const { return marquee_; }

    /// Sizes of the frame name labels the page draws, in CSS pixels. A label sits above its
    /// frame's top-left corner; pressing it selects the frame and starts moving it.
    void setFrameLabelSizes(std::unordered_map<Id, Vec2> sizes);
    [[nodiscard]] Id frameLabelAt(Vec2 screen) const;
    void clearSelection();
    bool deleteSelection();
    bool undo();
    bool redo();

    /// Turns the selected frame into a component, or wraps the selection in a new component. Returns
    /// the component.
    std::optional<Id> createComponent();
    /// Adds an instance of `component` to its right and selects it.
    std::optional<Id> createInstance(const Id& component);
    /// Replaces an instance with ordinary nodes that look the same, as one undo step.
    std::optional<Id> detachInstance(const Id& instance);
    /// Drops every override of an instance, or only the selected copied layer's local override.
    bool resetOverrides(const Id& instance);
    /// Selects `id` and, if it isn't fully visible in a `viewport` of that screen size, pans to
    /// center it.
    void reveal(const Id& id, Vec2 viewport);

    /// Applies a document edit made outside the canvas, such as a token change, as one undo step.
    bool edit(const OpBody& op);
    /// Adds a token with a free `<family>-<n>` identifier and a starting value, last in its family.
    std::optional<Id> addToken(TokenType type);
    /// Adds a mode named `Mode <n>`, last in order.
    std::optional<Id> addMode();
    bool deleteMode(const Id& mode);

    /// The mode tokens resolve in on the canvas. View state: switching it is not an edit.
    void setActiveMode(const Id& mode);
    [[nodiscard]] const Id& activeMode() const { return activeMode_; }

    /// When on (the default), drawing and moving put edges on whole world units, in absolute
    /// coordinates, so shapes render crisp at 100% zoom regardless of where their parent sits.
    void setPixelSnap(bool enabled);
    [[nodiscard]] bool pixelSnap() const { return pixelSnap_; }

    /// World bounds of the single selected node, the one resize handles attach to.
    [[nodiscard]] std::optional<Rect> handleBounds() const;
    /// The handle under `screen`, if any.
    [[nodiscard]] Handle handleAt(Vec2 screen) const;
    /// CSS cursor for the pointer at its last position, given the tool and gesture.
    [[nodiscard]] std::string_view cursor() const;

    /// Increases whenever the document, the selection or the tool changes.
    [[nodiscard]] std::uint64_t revision() const { return session_.revision() + revision_; }

    static constexpr double kDefaultShapeSize = 100.0;
    static constexpr double kClickSlopPx = 3.0;
    /// Corner handles are drawn this size and grabbed within it, in CSS pixels.
    static constexpr double kHandleSizePx = 8.0;
    /// How far from an edge it can be grabbed, in CSS pixels.
    static constexpr double kEdgeGrabPx = 4.0;
    /// Gap between a frame label and its frame, in CSS pixels.
    static constexpr double kLabelGapPx = 4.0;

private:
    enum class Gesture { None, Pan, Draw, Move, Resize, Marquee, PenCurve, Pencil };

    /// A node being moved and where it started, in world space.
    struct Moving {
        Id id;
        Vec2 start;
    };

    void update();
    void startDraw(Vec2 world);
    void updateDraw(Vec2 world, Vec2 screen);
    void previewDraw(Vec2 from, Vec2 to);
    void addPathPoint(Vec2 world);
    void previewPath(bool cursorPoint);
    void updatePenCurve(Vec2 world);
    void updatePencil(Vec2 world);
    void previewPencil();
    [[nodiscard]] Vec2 pathCursor(Vec2 world) const;
    void startMove(Vec2 world);
    void beginMoving();
    void updateMove(Vec2 world);
    void startMarquee();
    void updateMarquee(Vec2 world);
    void startResize(Handle handle);
    void updateResize(Vec2 world);
    void finish();
    void pruneSelection();
    [[nodiscard]] bool exists(const Id& id) const;
    /// True for nodes copied into an instance, which are edited through overrides.
    [[nodiscard]] bool isCopy(const Id& id) const;
    /// The node a click on `deepest` selects.
    [[nodiscard]] Id pickAt(const Id& deepest) const;
    /// The ops that apply `patch` to `id`: on an instance, style properties become an override of
    /// its component's root while geometry stays on the instance.
    [[nodiscard]] std::vector<OpBody> propsOps(const Id& id, const NodePatch& patch) const;
    void settlePropsPreview();
    [[nodiscard]] double snap(double world) const;
    [[nodiscard]] Vec2 originOf(const Id& parent) const;
    void touch() { ++revision_; }
    void fitPrototype();
    [[nodiscard]] Id prototypeHotspot(Vec2 screen) const;
    bool activatePrototype(Vec2 screen);
    [[nodiscard]] Vec2 prototypeOffset(const Id& screen) const;
    [[nodiscard]] bool prototypeOutsideOverlay(Vec2 screen) const;
    void cancelPrototypeTransition();

    Session& session_;
    Camera& camera_;
    ViewportController viewport_;
    Tool tool_ = Tool::Select;
    bool propsPreview_ = false;
    Id activeMode_ = kDefaultModeId;
    bool pixelSnap_ = true;
    std::vector<Id> selection_;
    std::uint64_t revision_ = 0;

    Gesture gesture_ = Gesture::None;
    Modifiers modifiers_;
    Vec2 lastScreen_{};
    Vec2 downScreen_{};
    Vec2 downWorld_{};
    Id target_;
    Id drawParent_;
    Vec2 parentOrigin_{};
    std::vector<Moving> moving_;
    std::vector<Id> marqueeBase_;
    std::optional<Rect> marquee_;
    std::unordered_map<Id, Vec2> labelSizes_;
    Handle handle_;

    /// Absolute bounds of the resized node when the gesture started.
    Rect resizeStart_{};
    bool resizeLineReverse_ = false;
    Path resizePath_;
    std::vector<Vec2> penPoints_;
    std::vector<Vec2> penInHandles_;
    std::vector<Vec2> penOutHandles_;
    bool penClosed_ = false;
    std::vector<Vec2> pencilPoints_;
    bool presenting_ = false;
    Camera prototypeSavedCamera_;
    Id prototypeScreen_;
    Id prototypeStart_;
    Vec2 prototypeViewport_{};
    std::vector<Id> prototypeHistory_;
    bool prototypeClick_ = false;
    Id prototypePressed_;
    struct PrototypeOverlay {
        Id screen;
        bool dismissOutside = true;
        OverlayPlacement placement = OverlayPlacement::Center;
        OverlayOffset offset{};
    };
    std::vector<PrototypeOverlay> prototypeOverlays_;
    bool prototypeOutsidePressed_ = false;
    std::vector<PrototypeLayer> prototypeOutgoing_;
    double prototypeDuration_ = 0;
    double prototypeElapsed_ = 0;
    bool prototypeOverlayTransition_ = false;
    PrototypeTransition prototypeTransition_ = PrototypeTransition::Instant;
};

[[nodiscard]] std::optional<Tool> toolFromName(std::string_view name);

} // namespace vc
