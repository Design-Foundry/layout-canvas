#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <string>
#include "vc/diagnostics.hpp"
#include "vc/document.hpp"
#include "vc/draw_list.hpp"
#include "vc/icon.hpp"
#include "vc/json.hpp"
#include "vc/native.h"
#include "vc/session.hpp"

using namespace vc;

namespace {

constexpr const char* kLeftHalf =
    R"(<svg xmlns="http://www.w3.org/2000/svg" height="24" viewBox="0 -960 960 960" width="24"><path d="M0-960H480V0H0Z"/></svg>)";
constexpr const char* kCircle =
    R"x(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24"><g transform="translate(2 2)"><circle cx="10" cy="10" r="8" fill="#000"/></g></svg>)x";

Id idOf(std::uint64_t n) {
    return {5, n};
}

Node frame(std::uint64_t id, const char* order = "a0") {
    Node n;
    n.id = idOf(id);
    n.type = NodeType::Frame;
    n.order = order;
    n.name = "Frame";
    n.fillVisible = false;
    n.w = 200;
    n.h = 100;
    return n;
}

IconRef ref(const char* glyph, IconAxes axes = {}) {
    IconRef r;
    r.set = kMaterialSymbolsSetId;
    r.glyph = glyph;
    r.axes = std::move(axes);
    return r;
}

Node icon(std::uint64_t id, std::uint64_t parent, const char* glyph, const char* order = "a0") {
    Node n;
    n.id = idOf(id);
    n.type = NodeType::Icon;
    n.parent = parent ? idOf(parent) : Id{};
    n.order = order;
    n.name = "Icon";
    n.w = n.h = 24;
    n.icon = ref(glyph);
    return n;
}

Document load(const std::string& snapshot) {
    const auto ops = snapshotFromJson(snapshot);
    REQUIRE(ops.has_value());
    Document doc;
    for (const OpBody& op : *ops) REQUIRE(doc.apply(op));
    return doc;
}

std::string nodeJson(int version, const std::string& icon) {
    return R"({"version":)" + std::to_string(version) +
           R"(,"modes":[],"tokens":[],"nodes":[{"id":"0192a4b1-7c3d-4e5f-8a6b-000000000002","type":"icon","order":"a0","icon":)" + icon + "}]}";
}

int coverageAt(const IconMask& mask, int x, int y) {
    return mask.coverage[std::size_t(y) * mask.side + x];
}

} // namespace

TEST_CASE("the built-in Material Symbols set offers the axes the source has", "[icons]") {
    const IconSet& set = materialSymbolsSet();
    REQUIRE(set.id == kMaterialSymbolsSetId);
    REQUIRE(set.identifier == "material-symbols");
    REQUIRE(set.axes.size() == 4);
    const IconAxis* style = set.axis("style");
    REQUIRE(style);
    REQUIRE(style->values.size() == 3);
    REQUIRE(style->defaultValue == IconAxisValue{std::string{"outlined"}});
    REQUIRE(set.axis("FILL")->values == std::vector<IconAxisValue>{0.0, 1.0});
    REQUIRE(set.axis("wght")->values.size() == 7);
    REQUIRE(set.axis("wght")->values.front() == IconAxisValue{100.0});
    REQUIRE(set.axis("wght")->values.back() == IconAxisValue{700.0});
    REQUIRE(set.axis("opsz")->values == std::vector<IconAxisValue>{20.0, 24.0, 40.0, 48.0});
    REQUIRE(findIconSet(kMaterialSymbolsSetId) == &builtinIconSets().front());
    REQUIRE(Document{}.iconSets().size() == 1);
}

TEST_CASE("the path form maps to the structured reference and back", "[icons]") {
    const auto parsed = parseIconPath("material-symbols/arrow_back?style=rounded&fill=1&wght=500");
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->set == kMaterialSymbolsSetId);
    REQUIRE(parsed->glyph == "arrow_back");
    REQUIRE(parsed->axes.size() == 3);
    REQUIRE(parsed->axes.at("style") == IconAxisValue{std::string{"rounded"}});
    REQUIRE(parsed->axes.at("FILL") == IconAxisValue{1.0});
    REQUIRE(iconPath(*parsed) == "material-symbols/arrow_back?style=rounded&FILL=1&wght=500");
    REQUIRE(*parseIconPath(iconPath(*parsed)) == *parsed);

    const auto plain = parseIconPath("material-symbols/home?style=outlined&wght=400&opsz=24&FILL=0");
    REQUIRE(plain.has_value());
    REQUIRE(plain->axes.empty());
    REQUIRE(iconPath(*plain) == "material-symbols/home");
    REQUIRE(iconSourceKey(*plain) == "material-symbols/home?style=outlined&FILL=0&wght=400&opsz=24");
    REQUIRE(iconSourceKey(*parsed) == "material-symbols/arrow_back?style=rounded&FILL=1&wght=500&opsz=24");
    REQUIRE(parseIconPath("material-symbols/10k")->glyph == "10k");
}

TEST_CASE("the path form refuses what the set does not offer", "[icons]") {
    for (const char* bad : {"arrow_back", "material-symbols/", "material-symbols/Arrow", "material-symbols/a/b",
                            "lucide/arrow_back", "material-symbols/home?style=bold", "material-symbols/home?wght=450",
                            "material-symbols/home?fill=2", "material-symbols/home?grad=200", "material-symbols/home?wght",
                            "material-symbols/home?wght=400&wght=500", "material-symbols/home?opsz=30",
                            "material-symbols/home?wght=x"}) {
        const char* why = nullptr;
        INFO(bad);
        REQUIRE_FALSE(parseIconPath(bad, &why).has_value());
        REQUIRE(why != nullptr);
    }
}

TEST_CASE("an icon node takes the string form and stores the structured form", "[icons][json]") {
    Document doc = load(nodeJson(27, R"("material-symbols/arrow_back?style=rounded&fill=1&wght=500")"));
    const Node* node = doc.find(*Id::parse("0192a4b1-7c3d-4e5f-8a6b-000000000002"));
    REQUIRE(node);
    REQUIRE(node->type == NodeType::Icon);
    REQUIRE(node->icon.glyph == "arrow_back");
    REQUIRE(node->icon.axes.at("wght") == IconAxisValue{500.0});
    const std::string json = snapshotToJson(doc);
    REQUIRE(json.find("\"version\":27") != std::string::npos);
    REQUIRE(json.find("\"iconSets\"") != std::string::npos);
    REQUIRE(json.find("\"glyph\":\"arrow_back\"") != std::string::npos);
    REQUIRE(json.find("material-symbols/arrow_back") == std::string::npos);
    Document copy = load(json);
    REQUIRE(snapshotToJson(copy) == json);
    REQUIRE(copy.find(node->id)->icon == node->icon);
}

TEST_CASE("the object form needs only a glyph and normalizes axes", "[icons][json]") {
    Document doc = load(nodeJson(27, R"({"glyph":"home","axes":{"style":"sharp","FILL":0,"wght":300}})"));
    const Node& node = *doc.find(*Id::parse("0192a4b1-7c3d-4e5f-8a6b-000000000002"));
    REQUIRE(node.icon.set == kMaterialSymbolsSetId);
    REQUIRE(node.icon.axes.size() == 2);
    REQUIRE_FALSE(node.icon.axes.contains("FILL"));
    for (const char* bad : {R"({"glyph":"home","axes":{"wght":450}})", R"({"axes":{}})", R"({"glyph":"Home"})",
                            R"({"glyph":"home","extra":1})", R"({"set":"0192a4b1-7c3d-4e5f-8a6b-000000000099","glyph":"home"})",
                            "42"}) {
        INFO(bad);
        REQUIRE_FALSE(snapshotFromJson(nodeJson(27, bad)).has_value());
    }
}

TEST_CASE("icons need snapshot 27 and documents without icons do not write it", "[icons][json]") {
    clearDiagnostic();
    REQUIRE_FALSE(snapshotFromJson(nodeJson(26, R"("material-symbols/home")")).has_value());
    REQUIRE(lastDiagnostic() == Diagnostic{"nodes[0]", "icon layers need snapshot version 27 or later"});
    REQUIRE_FALSE(snapshotFromJson(R"({"version":26,"modes":[],"tokens":[],"nodes":[],"iconSets":[]})").has_value());

    Document doc;
    REQUIRE(doc.apply(CreateNode{frame(1)}));
    REQUIRE(snapshotToJson(doc).find("iconSets") == std::string::npos);
    REQUIRE(doc.apply(CreateNode{icon(2, 1, "home")}));
    REQUIRE(snapshotToJson(doc).find("\"version\":27") != std::string::npos);
    REQUIRE(doc.apply(DeleteNode{idOf(2)}));
    REQUIRE(snapshotToJson(doc).find("iconSets") == std::string::npos);
    REQUIRE(snapshotToJson(doc).find("\"version\":27") == std::string::npos);
}

TEST_CASE("an icon is validated, edited, undone and redone", "[icons]") {
    Session session{IdGenerator{3}};
    REQUIRE(session.edit(CreateNode{frame(1)}));
    REQUIRE(session.edit(CreateNode{icon(2, 1, "home")}));

    NodePatch swap;
    swap.icon = ref("search", {{"style", std::string{"rounded"}}, {"wght", 600.0}});
    REQUIRE(session.edit(SetProps{idOf(2), swap}));
    REQUIRE(session.document().find(idOf(2))->icon.glyph == "search");
    REQUIRE(session.undo());
    REQUIRE(session.document().find(idOf(2))->icon.glyph == "home");
    REQUIRE(session.redo());
    REQUIRE(session.document().find(idOf(2))->icon.axes.at("wght") == IconAxisValue{600.0});

    NodePatch bad;
    bad.icon = ref("search", {{"wght", 450.0}});
    REQUIRE_FALSE(session.edit(SetProps{idOf(2), bad}));
    bad.icon = ref("Search");
    REQUIRE_FALSE(session.edit(SetProps{idOf(2), bad}));
    IconRef foreign = ref("home");
    foreign.set = idOf(77);
    bad.icon = foreign;
    REQUIRE_FALSE(session.edit(SetProps{idOf(2), bad}));

    NodePatch onFrame;
    onFrame.icon = ref("home");
    REQUIRE_FALSE(session.edit(SetProps{idOf(1), onFrame}));
    Node rect;
    rect.id = idOf(3);
    rect.type = NodeType::Rect;
    rect.parent = idOf(1);
    rect.order = "a1";
    rect.icon = ref("home");
    REQUIRE_FALSE(session.edit(CreateNode{rect}));
    Node noGlyph = icon(4, 1, "home", "a2");
    noGlyph.icon = IconRef{};
    REQUIRE_FALSE(session.edit(CreateNode{noGlyph}));
}

TEST_CASE("an icon is a tinted, fixed leaf with no stroke or shadows", "[icons]") {
    Document doc;
    REQUIRE(doc.apply(CreateNode{frame(1)}));
    Node tinted = icon(2, 1, "home");
    tinted.fill = Oklch{0.6, 0.1, 200.0, 1.0};
    tinted.opacity = 0.5;
    REQUIRE(doc.apply(CreateNode{tinted}));
    NodePatch stroke;
    Stroke s;
    s.width = 2.0;
    stroke.stroke = Value<Stroke>{s};
    REQUIRE_FALSE(doc.apply(SetProps{idOf(2), stroke}));
    NodePatch clip;
    clip.clipContent = true;
    REQUIRE_FALSE(doc.apply(SetProps{idOf(2), clip}));
    NodePatch sizing;
    sizing.widthSizing = Sizing::Fill;
    sizing.heightSizing = Sizing::Hug;
    REQUIRE(doc.apply(SetProps{idOf(2), sizing}));
    REQUIRE_FALSE(canHaveChildren(NodeType::Icon));
    Node child = icon(3, 2, "home");
    REQUIRE_FALSE(doc.apply(CreateNode{child}));
}

TEST_CASE("an unregistered glyph loads and draws a missing-glyph box", "[icons][render]") {
    Document doc;
    REQUIRE(doc.apply(CreateNode{frame(1)}));
    REQUIRE(doc.apply(CreateNode{icon(2, 1, "glyph_nobody_has")}));
    std::vector<DrawItem> items;
    buildDrawList(doc, {-10, -10, 300, 200}, kDefaultModeId, items);
    REQUIRE(items.size() == 1);
    REQUIRE(items[0].kind == DrawKind::Stroke);
    REQUIRE(items[0].quad.w == 24);
    REQUIRE(items[0].quad.h == 24);
    REQUIRE(snapshotToJson(doc).find("glyph_nobody_has") != std::string::npos);
}

TEST_CASE("a registered glyph draws its mask through the glyph atlas, sharp at each zoom bucket", "[icons][render]") {
    const std::string key = iconSourceKey(ref("test_left_half"));
    REQUIRE(registerIconSvg(key, kLeftHalf) == nullptr);
    REQUIRE(registerIconSvg(key, kLeftHalf) == nullptr);
    REQUIRE(registerIconSvg(key, kCircle) != nullptr);
    REQUIRE(iconRegistered(key));

    Document doc;
    REQUIRE(doc.apply(CreateNode{frame(1)}));
    Node node = icon(2, 1, "test_left_half");
    node.x = 10;
    node.y = 20;
    node.w = 48;
    node.h = 24;
    REQUIRE(doc.apply(CreateNode{node}));
    std::vector<DrawItem> items;
    buildDrawList(doc, {-10, -10, 300, 200}, kDefaultModeId, items);
    REQUIRE(items.size() == 1);
    REQUIRE(items[0].kind == DrawKind::Glyph);
    REQUIRE(items[0].quad.w == 24);
    REQUIRE(items[0].quad.x == 22);
    REQUIRE(items[0].quad.y == 20);
    const Rect small = items[0].a.rect;
    REQUIRE(small.w > 0);

    buildDrawList(doc, {-10, -10, 300, 200}, kDefaultModeId, items, {}, {}, 8.0);
    REQUIRE(items.size() == 1);
    REQUIRE(items[0].a.rect.w > small.w);
    REQUIRE(items[0].a.rect.w == 256.0 / kTextAtlasSize);

    const auto atlas = textAtlas();
    const auto at = [&](const Rect& uv, double fx, double fy) {
        return int(atlas[std::size_t(uv.y * kTextAtlasSize + fy * uv.h * kTextAtlasSize) * kTextAtlasSize +
                         std::size_t(uv.x * kTextAtlasSize + fx * uv.w * kTextAtlasSize)]);
    };
    REQUIRE(at(items[0].a.rect, 0.25, 0.5) == 255);
    REQUIRE(at(items[0].a.rect, 0.75, 0.5) == 0);
    REQUIRE(iconRasterSide(24) == 32);
    REQUIRE(iconRasterSide(8) == 16);
    REQUIRE(iconRasterSide(100000) == 512);
}

TEST_CASE("nanosvg rasterizes a small known SVG to the expected coverage", "[icons][render]") {
    const auto half = rasterizeIconSvg(kLeftHalf, 24);
    REQUIRE(half.has_value());
    REQUIRE(half->side == 24);
    REQUIRE(coverageAt(*half, 3, 12) == 255);
    REQUIRE(coverageAt(*half, 11, 0) == 255);
    REQUIRE(coverageAt(*half, 11, 23) == 255);
    REQUIRE(coverageAt(*half, 12, 12) == 0);
    REQUIRE(coverageAt(*half, 20, 5) == 0);

    const auto circle = rasterizeIconSvg(kCircle, 48);
    REQUIRE(circle.has_value());
    REQUIRE(coverageAt(*circle, 24, 24) == 255);
    REQUIRE(coverageAt(*circle, 1, 1) == 0);
    REQUIRE(coverageAt(*circle, 46, 46) == 0);
    const int edge = coverageAt(*circle, 12, 12);
    REQUIRE(edge > 0);
    REQUIRE(edge < 255);

    const auto doubled = rasterizeIconSvg(kLeftHalf, 64);
    REQUIRE(doubled->side == 64);
    REQUIRE(coverageAt(*doubled, 31, 32) == 255);
    REQUIRE(coverageAt(*doubled, 33, 32) == 0);
}

TEST_CASE("strokes and shapes of the accepted subset rasterize", "[icons][render]") {
    const auto line = rasterizeIconSvg(
        R"(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24"><line x1="2" y1="12" x2="22" y2="12" stroke="#000" stroke-width="4" fill="none"/><rect x="0" y="0" width="4" height="4"/></svg>)", 24);
    REQUIRE(line.has_value());
    REQUIRE(coverageAt(*line, 12, 12) == 255);
    REQUIRE(coverageAt(*line, 12, 4) == 0);
    REQUIRE(coverageAt(*line, 1, 1) == 255);
}

TEST_CASE("only the safe SVG subset is accepted", "[icons]") {
    REQUIRE(iconSvgProblem(kLeftHalf) == nullptr);
    REQUIRE(iconSvgProblem(kCircle) == nullptr);
    REQUIRE(iconSvgProblem(R"(<?xml version="1.0"?><svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1 1"><!-- c --><path d="M0 0L1 1"/></svg>)") == nullptr);
    for (const char* bad : {
             "", "not xml", "<svg",
             R"(<svg><script>alert(1)</script></svg>)",
             R"(<svg><image href="http://example.com/a.png"/></svg>)",
             R"(<svg><use href="#a"/></svg>)",
             R"(<svg><foreignObject/></svg>)",
             R"(<svg><style>path{fill:red}</style></svg>)",
             R"(<svg><defs><linearGradient id="g"/></defs></svg>)",
             R"(<!DOCTYPE svg><svg/>)",
             R"(<!DOCTYPE svg [<!ENTITY a "b">]><svg/>)",
             R"(<svg><![CDATA[x]]></svg>)",
             R"x(<svg onload="x()"/>)x",
             R"x(<svg><path d="M0 0" fill="url(#g)"/></svg>)x",
             R"(<svg><path d="M0 0" fill="&amp;"/></svg>)",
             R"(<svg><path d=M0/></svg>)",
             R"(<svg></g></svg>)",
             R"(<svg/><svg/>)",
             R"(<svg><svg/></svg>)",
             R"(<path d="M0 0"/>)",
         }) {
        INFO(bad);
        REQUIRE(iconSvgProblem(bad) != nullptr);
        REQUIRE_FALSE(rasterizeIconSvg(bad, 16).has_value());
        REQUIRE(registerIconSvg("test/bad", bad) != nullptr);
    }
    std::string deep = "<svg>";
    for (int i = 0; i < 17; ++i) deep += "<g>";
    for (int i = 0; i < 17; ++i) deep += "</g>";
    REQUIRE(iconSvgProblem(deep + "</svg>") != nullptr);
    std::string many = "<svg>";
    for (int i = 0; i < 520; ++i) many += "<path d=\"M0 0\"/>";
    REQUIRE(iconSvgProblem(many + "</svg>") != nullptr);
    REQUIRE(iconSvgProblem(std::string("<svg><path d=\"") + std::string(kMaxIconSvgBytes, 'M') + "\"/></svg>") != nullptr);
    REQUIRE_FALSE(rasterizeIconSvg(kLeftHalf, 0).has_value());
    REQUIRE_FALSE(rasterizeIconSvg(kLeftHalf, kMaxIconRasterSide + 1).has_value());
}

TEST_CASE("the native interface registers icons and reports their keys", "[icons][native]") {
    char* key = vc_icon_source_key(R"("material-symbols/arrow_back?style=rounded&fill=1")");
    REQUIRE(key != nullptr);
    REQUIRE(std::string{key} == "material-symbols/arrow_back?style=rounded&FILL=1&wght=400&opsz=24");
    REQUIRE(vc_register_icon(key, kLeftHalf) == 1);
    REQUIRE(vc_register_icon(key, kLeftHalf) == 1);
    REQUIRE(vc_register_icon(key, kCircle) == 0);
    char* error = vc_last_error();
    REQUIRE(error != nullptr);
    REQUIRE(std::string{error}.find("different SVG") != std::string::npos);
    vc_free(error);
    REQUIRE(vc_register_icon("test/script", "<svg><script/></svg>") == 0);
    vc_free(vc_last_error());
    vc_free(key);
    REQUIRE(vc_icon_source_key(R"({"glyph":"Home"})") == nullptr);
    vc_free(vc_last_error());
    char* normalized = vc_icon_normalize(R"("material-symbols/home?wght=700&fill=0")");
    REQUIRE(normalized != nullptr);
    REQUIRE(std::string{normalized} == R"({"axes":{"wght":700},"glyph":"home","set":"00000000-0000-0000-0000-000000000200"})");
    vc_free(normalized);
    REQUIRE(vc_icon_normalize(R"("nope")") == nullptr);
    vc_free(vc_last_error());
    char* object = vc_icon_source_key(R"({"glyph":"home","axes":{"wght":700}})");
    REQUIRE(std::string{object} == "material-symbols/home?style=outlined&FILL=0&wght=700&opsz=24");
    vc_free(object);
}

namespace {

struct ComponentScene {
    Document doc;
    Id component = idOf(10), glyph = idOf(11), instance = idOf(20);

    ComponentScene() {
        REQUIRE(doc.apply(CreateNode{frame(1)}));
        Node main;
        main.id = component;
        main.type = NodeType::Component;
        main.parent = idOf(1);
        main.order = "a0";
        main.name = "Back button";
        main.w = main.h = 40;
        REQUIRE(doc.apply(CreateNode{main}));
        Node mark = icon(11, 10, "arrow_back");
        mark.name = "Mark";
        REQUIRE(doc.apply(CreateNode{mark}));
        Node copy;
        copy.id = instance;
        copy.type = NodeType::Instance;
        copy.parent = idOf(1);
        copy.component = component;
        copy.order = "a1";
        copy.x = 100;
        copy.w = copy.h = 40;
        REQUIRE(doc.apply(CreateNode{copy}));
    }

    const Node& copiedMark() const { return *doc.view(doc.viewChildren(instance).front()); }
};

} // namespace

TEST_CASE("an instance swaps the glyph and tint of an icon through an override", "[icons][components]") {
    ComponentScene s;
    REQUIRE(s.copiedMark().icon.glyph == "arrow_back");
    NodePatch patch;
    patch.icon = ref("close", {{"style", std::string{"sharp"}}});
    patch.fill = Oklch{0.7, 0.2, 30.0, 1.0};
    const auto inverse = s.doc.apply(SetOverride{s.instance, s.glyph, patch});
    REQUIRE(inverse.has_value());
    REQUIRE(s.copiedMark().icon.glyph == "close");
    REQUIRE(s.copiedMark().icon.axes.at("style") == IconAxisValue{std::string{"sharp"}});
    REQUIRE(s.doc.find(s.glyph)->icon.glyph == "arrow_back");
    REQUIRE(s.doc.apply(*inverse));
    REQUIRE(s.copiedMark().icon.glyph == "arrow_back");

    NodePatch wrong;
    wrong.icon = ref("close");
    REQUIRE_FALSE(s.doc.apply(SetOverride{s.instance, s.component, wrong}));

    Document reloaded = load(snapshotToJson(s.doc));
    REQUIRE(reloaded.view(reloaded.viewChildren(s.instance).front())->icon.glyph == "arrow_back");
}

TEST_CASE("an override that swaps an icon survives the snapshot", "[icons][components][json]") {
    ComponentScene s;
    NodePatch patch;
    patch.icon = ref("close", {{"FILL", 1.0}});
    REQUIRE(s.doc.apply(SetOverride{s.instance, s.glyph, patch}));
    const std::string json = snapshotToJson(s.doc);
    REQUIRE(json.find("\"version\":27") != std::string::npos);
    Document copy = load(json);
    REQUIRE(copy.view(copy.viewChildren(s.instance).front())->icon.axes.at("FILL") == IconAxisValue{1.0});
    REQUIRE(snapshotToJson(copy) == json);
    REQUIRE_FALSE(snapshotFromJson([&] {
        std::string old = json;
        old.replace(old.find("\"version\":27"), 12, "\"version\":26");
        return old;
    }()).has_value());
}

TEST_CASE("a variant rule swaps the glyph of an icon layer", "[icons][components]") {
    ComponentScene s;
    ComponentDefinition definition;
    definition.axes.push_back({"state", {"idle", "done"}, "idle"});
    VariantRule rule;
    rule.when = {{"state", "done"}};
    rule.target = s.glyph;
    rule.patch.icon = ref("check", {{"FILL", 1.0}});
    definition.rules.push_back(rule);
    REQUIRE(s.doc.apply(SetComponentDefinition{s.component, definition}));
    REQUIRE(s.copiedMark().icon.glyph == "arrow_back");

    NodePatch values;
    values.componentValues = ComponentValues{{"state", std::string{"done"}}};
    REQUIRE(s.doc.apply(SetProps{s.instance, values}));
    REQUIRE(s.copiedMark().icon.glyph == "check");

    ComponentDefinition onWrongLayer = definition;
    onWrongLayer.rules[0].target = s.component;
    REQUIRE_FALSE(s.doc.apply(SetComponentDefinition{s.component, onWrongLayer}));
    Document reloaded = load(snapshotToJson(s.doc));
    REQUIRE(reloaded.view(reloaded.viewChildren(s.instance).front())->icon.glyph == "check");
}

TEST_CASE("cloning keeps icons and clone patches can swap them", "[icons][components]") {
    Document doc;
    REQUIRE(doc.apply(CreateNode{frame(1)}));
    Node mark = icon(2, 1, "home");
    mark.fill = Oklch{0.5, 0.1, 100.0, 1.0};
    REQUIRE(doc.apply(CreateNode{mark}));
    NodePatch swap;
    swap.icon = ref("settings", {{"wght", 200.0}});
    const auto inverse = doc.apply(CloneNode{idOf(1), idOf(50), Id{}, "a5", {Override{idOf(2), swap}}});
    REQUIRE(inverse.has_value());
    const Id copy = derivedId(idOf(50), idOf(2));
    REQUIRE(doc.find(copy)->icon.glyph == "settings");
    REQUIRE(doc.find(copy)->fill == mark.fill);
    REQUIRE(doc.find(idOf(2))->icon.glyph == "home");
    REQUIRE(doc.apply(*inverse));
    REQUIRE(doc.find(copy) == nullptr);
}

TEST_CASE("node inspection shows the structured reference and its path form", "[icons][json]") {
    Document doc;
    REQUIRE(doc.apply(CreateNode{frame(1)}));
    Node node = icon(2, 1, "arrow_back");
    node.icon.axes["style"] = std::string{"rounded"};
    REQUIRE(doc.apply(CreateNode{node}));
    const std::string inspected = inspectNodeJson(doc, *doc.find(idOf(2)), kDefaultModeId);
    REQUIRE(inspected.find("\"glyph\":\"arrow_back\"") != std::string::npos);
    REQUIRE(inspected.find("\"iconPath\":\"material-symbols/arrow_back?style=rounded\"") != std::string::npos);
}

TEST_CASE("an icon fill can be a color token", "[icons]") {
    Document doc;
    Token token;
    token.id = idOf(90);
    token.identifier = "brand";
    token.value = Oklch{0.6, 0.1, 200.0, 1.0};
    REQUIRE(doc.apply(CreateToken{token}));
    REQUIRE(doc.apply(CreateNode{frame(1)}));
    Node node = icon(2, 1, "home");
    node.fill = TokenRef{idOf(90)};
    REQUIRE(doc.apply(CreateNode{node}));
    REQUIRE(registerIconSvg(iconSourceKey(ref("home")), kLeftHalf) == nullptr);
    std::vector<DrawItem> items;
    buildDrawList(doc, {-10, -10, 300, 200}, kDefaultModeId, items);
    REQUIRE(items.size() == 1);
    REQUIRE(items[0].kind == DrawKind::Glyph);
    REQUIRE(items[0].color.a == 1.0f);
    REQUIRE(items[0].color.g != kUnresolvedColor.g);
}

TEST_CASE("the used icons list each reference once, overrides and rules included", "[icons][json]") {
    ComponentScene s;
    REQUIRE(s.doc.apply(CreateNode{icon(30, 1, "home")}));
    REQUIRE(s.doc.apply(CreateNode{icon(31, 1, "home", "a3")}));
    NodePatch swap;
    swap.icon = ref("close", {{"wght", 200.0}});
    REQUIRE(s.doc.apply(SetOverride{s.instance, s.glyph, swap}));
    ComponentDefinition definition;
    definition.axes.push_back({"state", {"idle", "done"}, "idle"});
    VariantRule rule;
    rule.when = {{"state", "done"}};
    rule.target = s.glyph;
    rule.patch.icon = ref("check");
    definition.rules.push_back(rule);
    REQUIRE(s.doc.apply(SetComponentDefinition{s.component, definition}));

    const std::string used = usedIconsToJson(s.doc);
    const auto count = [&](const std::string& needle) {
        std::size_t n = 0;
        for (auto at = used.find(needle); at != std::string::npos; at = used.find(needle, at + 1)) ++n;
        return n;
    };
    REQUIRE(count("\"key\"") == 4);
    for (const char* key : {"material-symbols/home?style=outlined&FILL=0&wght=400&opsz=24", "material-symbols/arrow_back?style=outlined&FILL=0&wght=400&opsz=24", "material-symbols/close?style=outlined&FILL=0&wght=200&opsz=24", "material-symbols/check?style=outlined&FILL=0&wght=400&opsz=24"}) {
        INFO(key);
        REQUIRE(count(std::string{"\"key\":\""} + key + "\"") == 1);
    }
    REQUIRE(usedIconsToJson(Document{}) == "[]");
}
