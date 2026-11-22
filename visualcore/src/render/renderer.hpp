#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "vc/camera.hpp"
#include "vc/document.hpp"
#include "vc/editor.hpp"
#include "vc/image_export.hpp"

namespace vc::render {

struct FrameStats {
    double frameMs = 0.0;
    double cpuMs = 0.0;
    std::size_t visible = 0;
    std::size_t total = 0;
};

using ExportPlanCallback = std::function<void(const ExportPlan&)>;
using ExportTileCallback = std::function<void(const TilePixels&, std::vector<std::uint8_t>&&)>;
/// Called once at the end; an empty message means every tile was delivered.
using ExportDoneCallback = std::function<void(const std::string&)>;

class Renderer {
public:
    /// Draws `document` through `camera`, with the selection and handles from `editor`, and keeps the
    /// canvas cursor in sync with it. All three must outlive the renderer.
    Renderer(std::string canvasSelector, const Camera& camera, const Document& document, Editor& editor);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void start();
    void stop();

    /// Canvas clear color in 0..1 sRGB; alpha 0 leaves the page behind the canvas visible.
    void setBackground(float r, float g, float b, float a);

    /// Renders node `id` and its descendants offscreen at `scale`, in the document's current token mode,
    /// without any canvas overlay and with a transparent background. `onPlan` reports the final size,
    /// scale and tiling; `onTile` receives straight-alpha RGBA tiles; `onDone` ends the export.
    void exportNode(const Id& id, double scale, ExportPlanCallback onPlan, ExportTileCallback onTile,
                    ExportDoneCallback onDone);

    [[nodiscard]] FrameStats stats() const;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace vc::render
