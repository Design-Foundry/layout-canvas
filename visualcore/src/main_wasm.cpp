#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>

#include <emscripten/bind.h>
#include <emscripten/html5.h>

#include "render/renderer.hpp"
#include "vc/camera.hpp"
#include "vc/demo.hpp"
#include "vc/editor.hpp"
#include "vc/geometry.hpp"
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
        : renderer{selector, camera, session.document(), editor} {}
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
        rt->editor.stopPrototype();
        rt->renderer.stop();
        emscripten_html5_remove_all_event_listeners();
    }
}

void addRandomShapes(int count, int seed) {
    if (rt && !rt->editor.presenting()) {
        vc::addRandomNodes(rt->session, static_cast<std::size_t>(count), {-5000, -5000, 10000, 10000},
                           static_cast<std::uint64_t>(seed));
    }
}

bool loadDocument(const std::string& snapshot) {
    if (!rt) {
        return false;
    }
    rt->editor.stopPrototype();
    rt->editor.pointerCancel();
    rt->editor.clearSelection();
    rt->session = vc::Session{};
    if (snapshot.empty()) {
        return true;
    }
    const auto ops = vc::snapshotFromJson(snapshot);
    if (!ops) {
        return false;
    }
    for (const vc::OpBody& op : *ops) {
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
    return rt->session.receive(*parsed);
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

double structureRevision() {
    return rt ? static_cast<double>(rt->session.structureRevision()) : 0.0;
}

std::string layers() {
    return rt ? vc::layersToJson(rt->session.document()) : "[]";
}

std::string frameLabels() {
    return rt && !rt->editor.presenting() ? vc::frameLabelsToJson(rt->session.document(), rt->camera, rt->editor.activeMode()) : "[]";
}

std::string jsonString(const std::string& value) {
    std::string out="\"";
    constexpr char hex[]="0123456789abcdef";
    for(const unsigned char ch:value) {
        if(ch=='"' || ch=='\\') {out+='\\';out+=char(ch);}
        else if(ch<32) {out+="\\u00";out+=hex[ch>>4];out+=hex[ch&15];}
        else out+=char(ch);
    }
    return out+'"';
}

vc::Vec2 prototypeViewport() {
    double width=0,height=0;
    if(rt) emscripten_get_element_css_size(attachedSelector.c_str(),&width,&height);
    return {width,height};
}

bool startPrototype(const std::string& id) {
    const auto parsed=vc::Id::parse(id);
    return rt && parsed && rt->editor.startPrototype(*parsed,prototypeViewport());
}
void stopPrototype() {if(rt)rt->editor.stopPrototype();}
bool prototypeBack() {return rt && rt->editor.prototypeBack();}
bool restartPrototype() {return rt && rt->editor.restartPrototype();}
bool closePrototypeOverlay() {return rt && rt->editor.closePrototypeOverlay();}
void setPrototypeReducedMotion(bool reduced) {if(rt) rt->editor.setPrototypeReducedMotion(reduced);}

std::string prototypeState() {
    if(!rt) return "{\"active\":false,\"screen\":null,\"start\":null,\"canBack\":false,\"overlay\":null,\"transitioning\":false}";
    rt->editor.resizePrototype(prototypeViewport());
    const auto& editor=rt->editor;
    if(!editor.presenting()) return "{\"active\":false,\"screen\":null,\"start\":null,\"canBack\":false,\"overlay\":null,\"transitioning\":false}";
    return "{\"active\":true,\"screen\":"+jsonString(editor.prototypeScreen().toString())+
        ",\"start\":"+jsonString(editor.prototypeStart().toString())+",\"canBack\":"+(editor.canPrototypeBack()?"true":"false")+
        ",\"overlay\":"+(editor.prototypeOverlay().isNil()?"null":jsonString(editor.prototypeOverlay().toString()))+
        ",\"transitioning\":"+(editor.prototypeTransitioning()?"true":"false")+"}";
}

std::string prototypeScreens() {
    std::string out="[";
    if(rt) for(const auto& id:rt->session.document().children(vc::Id{})) {
        if(!rt->editor.prototypeEligible(id)) continue;
        const auto* node=rt->session.document().find(id);
        const auto box=rt->session.document().box(id,rt->editor.activeMode());
        if(out.size()>1) out+=',';
        out+="{\"id\":"+jsonString(id.toString())+",\"name\":"+jsonString(node->name)+",\"flowName\":"+jsonString(node->prototypeFlowName)+
            ",\"start\":"+(node->prototypeStart?"true":"false")+",\"w\":"+std::to_string(box.w)+",\"h\":"+std::to_string(box.h)+"}";
    }
    return out+"]";
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

bool toggleSelection(const std::string& id) {
    const auto parsed = vc::Id::parse(id);
    if (!rt || !parsed) {
        return false;
    }
    rt->editor.toggleSelection(*parsed);
    return true;
}

/// Sizes as `id:width:height` entries separated by `;`, measured by the page.
void setFrameLabelSizes(const std::string& sizes) {
    if (!rt) {
        return;
    }
    std::unordered_map<vc::Id, vc::Vec2> parsed;
    std::size_t start = 0;
    while (start < sizes.size()) {
        const std::size_t end = std::min(sizes.find(';', start), sizes.size());
        const std::string entry = sizes.substr(start, end - start);
        const std::size_t first = entry.find(':');
        const std::size_t second = entry.find(':', first + 1);
        if (first != std::string::npos && second != std::string::npos) {
            if (const auto id = vc::Id::parse(entry.substr(0, first))) {
                parsed[*id] = {std::stod(entry.substr(first + 1, second - first - 1)), std::stod(entry.substr(second + 1))};
            }
        }
        start = end + 1;
    }
    rt->editor.setFrameLabelSizes(std::move(parsed));
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
    static const char* const kNames[] = {"select", "frame", "rectangle", "ellipse", "hand", "text", "line", "pen", "pencil", "polygon", "star"};
    return rt ? kNames[static_cast<int>(rt->editor.tool())] : "select";
}

std::string node(const std::string& id) {
    const auto parsed = vc::Id::parse(id);
    const vc::Node* found = rt && parsed ? rt->session.document().find(*parsed) : nullptr;
    if (!found && rt && parsed) found = rt->session.document().view(*parsed);
    return found ? vc::inspectNodeJson(rt->session.document(), *found, rt->editor.activeMode()) : "null";
}

std::string screenBounds(const std::string& id) {
    const auto parsed = vc::Id::parse(id);
    const auto bounds = rt && parsed ? vc::worldBounds(rt->session.document(), *parsed, rt->editor.activeMode()) : std::nullopt;
    if (!bounds) return "null";
    const auto point = rt->camera.toScreen({bounds->x, bounds->y});
    return "{\"x\":" + std::to_string(point.x) + ",\"y\":" + std::to_string(point.y) +
        ",\"w\":" + std::to_string(bounds->w * rt->camera.zoom()) + ",\"h\":" +
        std::to_string(bounds->h * rt->camera.zoom()) + "}";
}

bool setProps(const std::string& id, const std::string& patch) {
    const auto parsedId = vc::Id::parse(id);
    const auto parsedPatch = vc::patchFromJson(patch);
    return rt && parsedId && parsedPatch && rt->editor.setProps(*parsedId, *parsedPatch);
}

std::string importImage(const std::string& image, const std::string& name) {
    if(!rt) return "";
    const auto parsed=vc::patchFromJson("{\"image\":"+image+"}");
    if(!parsed || !parsed->image) return "";
    const auto id=rt->editor.importImage(*parsed->image,name);
    return id?id->toString():"";
}

bool previewProps(const std::string& id, const std::string& patch) {
    const auto parsedId = vc::Id::parse(id);
    const auto parsedPatch = vc::patchFromJson(patch);
    return rt && parsedId && parsedPatch && rt->editor.previewProps(*parsedId, *parsedPatch);
}

bool commitPropsPreview() {
    return rt && rt->editor.commitPropsPreview();
}

void cancelPropsPreview() {
    if (rt) {
        rt->editor.cancelPropsPreview();
    }
}

std::string colorToHex(const std::string& color) {
    const auto patch = vc::patchFromJson("{\"fill\":" + color + "}");
    const auto* oklch = patch && patch->fill ? std::get_if<vc::Oklch>(&*patch->fill) : nullptr;
    return oklch ? vc::toHex(*oklch) : "";
}

std::string hexToColor(const std::string& hex, double alpha) {
    const auto color = vc::fromHex(hex, alpha);
    return color ? vc::colorToJson(*color) : "null";
}

double tokenRevision() {
    return rt ? static_cast<double>(rt->session.tokenRevision()) : 0.0;
}

std::string tokens() {
    return rt ? vc::tokensToJson(rt->session.document(), rt->editor.activeMode()) : "[]";
}

std::string modes() {
    return rt ? vc::modesToJson(rt->session.document()) : "[]";
}

std::string activeMode() {
    return rt ? rt->editor.activeMode().toString() : vc::kDefaultModeId.toString();
}

void setCanvasBackground(float r, float g, float b, float a) {
    if (rt) rt->renderer.setBackground(r, g, b, a);
}

bool setActiveMode(const std::string& id) {
    const auto parsed = vc::Id::parse(id);
    if (!rt || !parsed) {
        return false;
    }
    rt->editor.setActiveMode(*parsed);
    return rt->editor.activeMode() == *parsed;
}

std::string addToken(const std::string& type) {
    const auto parsed = vc::tokenTypeFromName(type);
    const auto id = rt && parsed ? rt->editor.addToken(*parsed) : std::nullopt;
    return id ? id->toString() : "";
}

bool replaceToken(const std::string& token) {
    auto parsed = vc::tokenFromJson(token);
    return rt && parsed && rt->editor.edit(vc::ReplaceToken{std::move(*parsed)});
}

bool deleteToken(const std::string& id) {
    const auto parsed = vc::Id::parse(id);
    return rt && parsed && rt->editor.edit(vc::DeleteToken{*parsed});
}

std::string addMode() {
    const auto id = rt ? rt->editor.addMode() : std::nullopt;
    return id ? id->toString() : "";
}

bool replaceMode(const std::string& mode) {
    auto parsed = vc::modeFromJson(mode);
    return rt && parsed && rt->editor.edit(vc::ReplaceMode{std::move(*parsed)});
}

bool deleteMode(const std::string& id) {
    const auto parsed = vc::Id::parse(id);
    return rt && parsed && rt->editor.deleteMode(*parsed);
}

std::string createComponent() {
    const auto id = rt ? rt->editor.createComponent() : std::nullopt;
    return id ? id->toString() : "";
}

std::string createInstance(const std::string& component) {
    const auto parsed = vc::Id::parse(component);
    const auto id = rt && parsed ? rt->editor.createInstance(*parsed) : std::nullopt;
    return id ? id->toString() : "";
}

std::string detachInstance(const std::string& instance) {
    const auto parsed = vc::Id::parse(instance);
    const auto id = rt && parsed ? rt->editor.detachInstance(*parsed) : std::nullopt;
    return id ? id->toString() : "";
}

bool resetOverrides(const std::string& instance) {
    const auto parsed = vc::Id::parse(instance);
    return rt && parsed && rt->editor.resetOverrides(*parsed);
}

bool setComponentDefinition(const std::string& component, const std::string& data) {
    const auto id = vc::Id::parse(component);
    const auto definition = vc::componentDefinitionFromJson(data);
    return rt && id && definition && rt->editor.setComponentDefinition(*id, *definition);
}

bool reveal(const std::string& id, double width, double height) {
    const auto parsed = vc::Id::parse(id);
    if (!rt || !parsed) {
        return false;
    }
    rt->editor.reveal(*parsed, {width, height});
    return true;
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

bool finishPath() { return rt && rt->editor.finishPath(); }
void cancelPath() { if (rt) rt->editor.cancelPath(); }
void cancelGesture() { if (rt) rt->editor.pointerCancel(); }
bool popPathPoint() { return rt && rt->editor.popPathPoint(); }
bool drawingPath() { return rt && rt->editor.drawingPath(); }

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
    function("structureRevision", &structureRevision);
    function("layers", &layers);
    function("selection", &selection);
    function("frameLabels", &frameLabels);
    function("startPrototype", &startPrototype);
    function("stopPrototype", &stopPrototype);
    function("prototypeBack", &prototypeBack);
    function("restartPrototype", &restartPrototype);
    function("closePrototypeOverlay", &closePrototypeOverlay);
    function("setPrototypeReducedMotion", &setPrototypeReducedMotion);
    function("prototypeState", &prototypeState);
    function("prototypeScreens", &prototypeScreens);
    function("select", &select);
    function("setTool", &setTool);
    function("toggleSelection", &toggleSelection);
    function("setFrameLabelSizes", &setFrameLabelSizes);
    function("tool", &tool);
    function("node", &node);
    function("screenBounds", &screenBounds);
    function("setProps", &setProps);
    function("importImage", &importImage);
    function("previewProps", &previewProps);
    function("commitPropsPreview", &commitPropsPreview);
    function("cancelPropsPreview", &cancelPropsPreview);
    function("hexToColor", &hexToColor);
    function("colorToHex", &colorToHex);
    function("tokenRevision", &tokenRevision);
    function("tokens", &tokens);
    function("modes", &modes);
    function("activeMode", &activeMode);
    function("setActiveMode", &setActiveMode);
    function("setCanvasBackground", &setCanvasBackground);
    function("addToken", &addToken);
    function("replaceToken", &replaceToken);
    function("deleteToken", &deleteToken);
    function("addMode", &addMode);
    function("replaceMode", &replaceMode);
    function("deleteMode", &deleteMode);
    function("createComponent", &createComponent);
    function("createInstance", &createInstance);
    function("detachInstance", &detachInstance);
    function("resetOverrides", &resetOverrides);
    function("setComponentDefinition", &setComponentDefinition);
    function("reveal", &reveal);
    function("deleteSelection", &deleteSelection);
    function("nudge", &nudge);
    function("undo", &undo);
    function("redo", &redo);
    function("finishPath", &finishPath);
    function("cancelPath", &cancelPath);
    function("cancelGesture", &cancelGesture);
    function("popPathPoint", &popPathPoint);
    function("drawingPath", &drawingPath);
}
