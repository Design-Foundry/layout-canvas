#pragma once
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>
#include "vc/camera.hpp"
#include "vc/session.hpp"
#include "vc/viewport_controller.hpp"

namespace vc {

enum class Tool { Select, Frame, Rect, Ellipse, Hand };

enum class PointerButton { Primary = 0, Middle = 1, Secondary = 2 };

struct Modifiers {
    /// Square shapes while drawing; one axis while moving.
    bool shift = false;
    /// Draw from the center.
    bool alt = false;
};

/// Turns pointer input into camera moves and document edits for the active tool.
/// Screen positions are CSS pixels relative to the canvas.
class Editor {
public:
    Editor(Session& session, Camera& camera);

    void setTool(Tool tool);
    [[nodiscard]] Tool tool() const { return tool_; }

    void pointerDown(Vec2 screen, PointerButton button, Modifiers modifiers = {});
    void pointerMove(Vec2 screen, Modifiers modifiers = {});
    /// Modifier keys changed without the pointer moving. Updates the gesture in progress.
    void setModifiers(Modifiers modifiers);
    void pointerUp();
    /// Ends the gesture without applying it, for example when the pointer leaves the canvas.
    void pointerCancel();
    void wheel(Vec2 screen, double deltaY);
    /// Moves the selected nodes by `delta` world units, as one undo step.
    bool nudge(Vec2 delta);

    [[nodiscard]] const std::vector<Id>& selection() const { return selection_; }
    void select(const Id& id);
    void clearSelection();
    bool deleteSelection();
    bool undo();
    bool redo();

    /// When on (the default), drawing and moving put edges on whole world units, in absolute
    /// coordinates, so shapes render crisp at 100% zoom regardless of where their parent sits.
    void setPixelSnap(bool enabled);
    [[nodiscard]] bool pixelSnap() const { return pixelSnap_; }

    /// Increases whenever the document, the selection or the tool changes.
    [[nodiscard]] std::uint64_t revision() const { return session_.revision() + revision_; }

    static constexpr double kDefaultShapeSize = 100.0;
    static constexpr double kClickSlopPx = 3.0;

private:
    enum class Gesture { None, Pan, Draw, Move };

    void update();
    void startDraw(Vec2 world);
    void updateDraw(Vec2 world, Vec2 screen);
    void previewDraw(Vec2 from, Vec2 to);
    void startMove(Vec2 world);
    void updateMove(Vec2 world);
    void finish();
    void pruneSelection();
    [[nodiscard]] double snap(double world) const;
    [[nodiscard]] Vec2 originOf(const Id& parent) const;
    void touch() { ++revision_; }

    Session& session_;
    Camera& camera_;
    ViewportController viewport_;
    Tool tool_ = Tool::Select;
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
    /// Absolute position of the moved node when the gesture started.
    Vec2 targetStart_{};
};

[[nodiscard]] std::optional<Tool> toolFromName(std::string_view name);

} // namespace vc
