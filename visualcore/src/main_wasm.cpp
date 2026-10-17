#include <cstdio>
#include <memory>
#include <string>

#include <emscripten/bind.h>
#include <emscripten/html5.h>

#include "render/renderer.hpp"
#include "vc/camera.hpp"
#include "vc/scene.hpp"
#include "vc/viewport_controller.hpp"
#include "web/input.hpp"

namespace {

struct Runtime {
    vc::Camera camera;
    vc::Scene scene;
    vc::ViewportController controller{camera};
    vc::render::Renderer renderer;

    explicit Runtime(const std::string& selector) : renderer{selector, camera, scene} {}
};

std::unique_ptr<Runtime> rt;
std::string attachedSelector;

void attach(const std::string& selector) {
    if (rt && selector != attachedSelector) {
        std::printf("visualcore: already attached to %s\n", attachedSelector.c_str());
        return;
    }
    if (!rt) {
        attachedSelector = selector;
        rt = std::make_unique<Runtime>(attachedSelector);
    }
    vc::web::attachInput(attachedSelector.c_str(), rt->controller);
    rt->renderer.start();
}

void detach() {
    if (rt) {
        rt->renderer.stop();
        emscripten_html5_remove_all_event_listeners();
    }
}

void addRandomShapes(int count, int seed) {
    if (rt) {
        vc::addRandomShapes(rt->scene, static_cast<std::size_t>(count), {-5000, -5000, 10000, 10000},
                            static_cast<std::uint32_t>(seed));
    }
}

struct FrameStatsJs {
    double frameMs = 0.0;
    double cpuMs = 0.0;
    double visible = 0.0;
    double total = 0.0;
};

FrameStatsJs frameStats() {
    if (!rt) {
        return {};
    }
    const vc::render::FrameStats s = rt->renderer.stats();
    return {s.frameMs, s.cpuMs, double(s.visible), double(s.total)};
}

} // namespace

int main() {
    std::puts("visualcore: alive");
    return 0;
}

EMSCRIPTEN_BINDINGS(visualcore) {
    using namespace emscripten;

    value_object<vc::Vec2>("Vec2")
        .field("x", &vc::Vec2::x)
        .field("y", &vc::Vec2::y);

    value_object<vc::Rect>("Rect")
        .field("x", &vc::Rect::x)
        .field("y", &vc::Rect::y)
        .field("w", &vc::Rect::w)
        .field("h", &vc::Rect::h);

    value_object<FrameStatsJs>("FrameStats")
        .field("frameMs", &FrameStatsJs::frameMs)
        .field("cpuMs", &FrameStatsJs::cpuMs)
        .field("visible", &FrameStatsJs::visible)
        .field("total", &FrameStatsJs::total);

    class_<vc::Camera>("Camera")
        .constructor<>()
        .function("toScreen", &vc::Camera::toScreen)
        .function("toWorld", &vc::Camera::toWorld)
        .function("visibleWorld", &vc::Camera::visibleWorld)
        .function("pan", &vc::Camera::pan)
        .function("zoomAt", &vc::Camera::zoomAt)
        .function("position", &vc::Camera::position)
        .function("zoom", &vc::Camera::zoom);

    function("attach", &attach);
    function("detach", &detach);
    function("addRandomShapes", &addRandomShapes);
    function("frameStats", &frameStats);
}
