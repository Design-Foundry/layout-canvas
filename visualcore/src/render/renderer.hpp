#pragma once
#include <cstddef>
#include <memory>
#include <string>
#include "vc/camera.hpp"
#include "vc/scene.hpp"

namespace vc::render {

struct FrameStats {
    double frameMs = 0.0;
    double cpuMs = 0.0;
    std::size_t visible = 0;
    std::size_t total = 0;
};

class Renderer {
public:
    Renderer(std::string canvasSelector, const Camera& camera, const Scene& scene);
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
