#pragma once
#include <cstddef>
#include <memory>
#include <string>
#include <vector>
#include "vc/camera.hpp"
#include "vc/document.hpp"
#include "vc/editor.hpp"

namespace vc::render {

struct FrameStats {
    double frameMs = 0.0;
    double cpuMs = 0.0;
    std::size_t visible = 0;
    std::size_t total = 0;
};

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

    [[nodiscard]] FrameStats stats() const;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace vc::render
