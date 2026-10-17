#include <cstdio>
#include <memory>
#include <string>

#include <emscripten/bind.h>
#include <emscripten/html5.h>

#include "render/renderer.hpp"
#include "vc/camera.hpp"
#include "vc/demo.hpp"
#include "vc/editor.hpp"
#include "vc/json.hpp"
#include "vc/session.hpp"
#include "web/input.hpp"

namespace {

struct Runtime {
    vc::Camera camera;
    vc::Session session;
    vc::Editor editor{session, camera};
    vc::render::Renderer renderer;

    explicit Runtime(const std::string& selector)
        : renderer{selector, camera, session.document(), editor.selection()} {}
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
    vc::web::attachInput(attachedSelector.c_str(), rt->editor);
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
        vc::addRandomNodes(rt->session, static_cast<std::size_t>(count), {-5000, -5000, 10000, 10000},
                           static_cast<std::uint64_t>(seed));
    }
}

bool loadDocument(const std::string& snapshot) {
    if (!rt) {
        return false;
    }
    rt->editor.pointerCancel();
    rt->editor.clearSelection();
    rt->session = vc::Session{};
    if (snapshot.empty()) {
        return true;
    }
    const auto nodes = vc::snapshotFromJson(snapshot);
    if (!nodes) {
        return false;
    }
    for (const vc::CreateNode& op : *nodes) {
        rt->session.applyRemote(op);
    }
    return true;
}

int applyRemoteOps(const std::string& ops) {
    if (!rt) {
        return -1;
    }
    const auto parsed = vc::opsFromJson(ops);
    if (!parsed) {
        return -1;
    }
    int applied = 0;
    for (const vc::OpBody& op : *parsed) {
        applied += rt->session.applyRemote(op) ? 1 : 0;
    }
    return applied;
}

std::string takeOutbox() {
    return rt ? vc::opsToJson(rt->session.takeOutbox()) : "[]";
}

std::string snapshot() {
    return rt ? vc::snapshotToJson(rt->session.document()) : "";
}

double revision() {
    return rt ? static_cast<double>(rt->editor.revision()) : 0.0;
}

std::string layers() {
    return rt ? vc::layersToJson(rt->session.document()) : "[]";
}

std::string selection() {
    std::string out = "[";
    if (rt) {
        for (const vc::Id& id : rt->editor.selection()) {
            out += (out.size() > 1 ? ",\"" : "\"") + id.toString() + "\"";
        }
    }
    return out + "]";
}

bool select(const std::string& id) {
    const auto parsed = vc::Id::parse(id);
    if (!rt || !parsed) {
        return false;
    }
    rt->editor.select(*parsed);
    return true;
}

bool setTool(const std::string& name) {
    const auto tool = vc::toolFromName(name);
    if (!rt || !tool) {
        return false;
    }
    rt->editor.setTool(*tool);
    return true;
}

std::string tool() {
    static const char* const kNames[] = {"select", "frame", "rectangle", "ellipse", "hand"};
    return rt ? kNames[static_cast<int>(rt->editor.tool())] : "select";
}

bool deleteSelection() {
    return rt && rt->editor.deleteSelection();
}

bool nudge(double dx, double dy) {
    return rt && rt->editor.nudge({dx, dy});
}

bool undo() {
    return rt && rt->editor.undo();
}

bool redo() {
    return rt && rt->editor.redo();
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
    function("loadDocument", &loadDocument);
    function("applyRemoteOps", &applyRemoteOps);
    function("takeOutbox", &takeOutbox);
    function("snapshot", &snapshot);
    function("revision", &revision);
    function("layers", &layers);
    function("selection", &selection);
    function("select", &select);
    function("setTool", &setTool);
    function("tool", &tool);
    function("deleteSelection", &deleteSelection);
    function("nudge", &nudge);
    function("undo", &undo);
    function("redo", &redo);
}
