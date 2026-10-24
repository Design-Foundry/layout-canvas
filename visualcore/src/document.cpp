#include "vc/document.hpp"
#include "vc/layout.hpp"

#include <unordered_set>

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace vc {
namespace {

bool validTextPaint(const NodePatch& patch) {
    return (!patch.stroke || *patch.stroke == Value<Stroke>{Stroke{}}) &&
        (!patch.shadows || *patch.shadows == Value<std::vector<Shadow>>{std::vector<Shadow>{}}) &&
        (!patch.cornerRadius || *patch.cornerRadius == Value<double>{0.0}) &&
        (!patch.cornerRadii || *patch.cornerRadii == std::array<std::optional<Value<double>>, 4>{}) &&
        (!patch.layout || *patch.layout == Layout{});
}

bool nonNegative(const Value<double>& value) {
    const auto* literal = std::get_if<double>(&value);
    return !literal || *literal >= 0.0;
}

bool isValidPatch(const NodePatch& patch) {
    if(patch.image && !validRaster(*patch.image)) return false;
    if(patch.imageFit && *patch.imageFit!="cover" && *patch.imageFit!="contain" && *patch.imageFit!="stretch") return false;
    if(patch.imageCrop) {const auto& c=*patch.imageCrop;
        if(!std::isfinite(c.x)||!std::isfinite(c.y)||!std::isfinite(c.w)||!std::isfinite(c.h)||c.x<0||c.y<0||c.w<=0||c.h<=0||c.x+c.w>1||c.y+c.h>1) return false;
    }
    if (patch.path && !validPath(*patch.path)) return false;
    for (const auto* v : {&patch.x,&patch.y,&patch.w,&patch.h}) if (*v && !std::isfinite(**v)) return false;
    if (patch.lineWidth && (!std::isfinite(*patch.lineWidth) || *patch.lineWidth < 0 || *patch.lineWidth > 4096)) return false;
    if (patch.text && patch.textStyle) return false;
    if (patch.textStyle) {
        if (*patch.textStyle == TextStyle{}) return false;
        TextRun run;
        if (patch.textStyle->font) run.font = *patch.textStyle->font;
        if (patch.textStyle->fontSize) run.fontSize = *patch.textStyle->fontSize;
        if (patch.textStyle->lineHeight) run.lineHeight = *patch.textStyle->lineHeight;
        if (patch.textStyle->letterSpacing) run.letterSpacing = *patch.textStyle->letterSpacing;
        if (patch.textStyle->align) run.align = *patch.textStyle->align;
        if (!validText(run)) return false;
    }
    if (patch.text && !validText(*patch.text)) return false;
    if ((patch.w && *patch.w < 0.0) || (patch.h && *patch.h < 0.0)) return false;
    if (patch.opacity) {
        const auto* opacity = std::get_if<double>(&*patch.opacity);
        if (opacity && (*opacity < 0.0 || *opacity > 1.0)) return false;
    }
    if (patch.cornerRadius && !nonNegative(*patch.cornerRadius)) return false;
    if (patch.cornerRadii) {
        for (const auto& corner : *patch.cornerRadii) {
            if (corner && !nonNegative(*corner)) return false;
        }
    }
    if (patch.stroke) {
        if (const auto* stroke = std::get_if<Stroke>(&*patch.stroke)) {
            if (!nonNegative(stroke->width)) return false;
            for (const auto& side : stroke->sides) {
                if (side && !nonNegative(*side)) return false;
            }
        }
    }
    if (patch.shadows) {
        if (const auto* layers = std::get_if<std::vector<Shadow>>(&*patch.shadows)) {
            for (const Shadow& layer : *layers) {
                if (layer.blur < 0.0) return false;
            }
        }
    }
    if (patch.layout) {
        if (!nonNegative(patch.layout->gap)) return false;
        for (const auto& side : patch.layout->padding) {
            if (!nonNegative(side)) return false;
        }
    }
    if (patch.limits) {
        for (const auto* limit : {&patch.limits->minWidth, &patch.limits->maxWidth, &patch.limits->minHeight,
                                  &patch.limits->maxHeight}) {
            if (*limit && !nonNegative(**limit)) return false;
        }
    }
    return true;
}

} // namespace

Document::Document() {
    modes_.emplace(kDefaultModeId, Mode{kDefaultModeId, "Default", "a0"});
}

std::optional<OpBody> Document::apply(const OpBody& op) {
    auto inverse = std::visit(
        [this](const auto& body) -> std::optional<OpBody> {
            using T = std::decay_t<decltype(body)>;
            if constexpr (std::is_same_v<T, CreateNode>) {
                return create(body);
            } else if constexpr (std::is_same_v<T, DeleteNode>) {
                return remove(body);
            } else if constexpr (std::is_same_v<T, SetProps>) {
                return setProps(body);
            } else if constexpr (std::is_same_v<T, MoveNode>) {
                return move(body);
            } else if constexpr (std::is_same_v<T, CreateToken>) {
                return createToken(body);
            } else if constexpr (std::is_same_v<T, DeleteToken>) {
                return deleteToken(body);
            } else if constexpr (std::is_same_v<T, ReplaceToken>) {
                return replaceToken(body);
            } else if constexpr (std::is_same_v<T, CreateMode>) {
                return createMode(body);
            } else if constexpr (std::is_same_v<T, DeleteMode>) {
                return deleteMode(body);
            } else if constexpr (std::is_same_v<T, ReplaceMode>) {
                return replaceMode(body);
            } else if constexpr (std::is_same_v<T, SetOverride>) {
                return setOverride(body);
            } else {
                return setComponentDefinition(body);
            }
        },
        op);
    if (inverse) {
        layoutDirty_ = true;
        treeDirty_ = true;
        std::size_t bytes=0;std::unordered_set<std::string> assets;
        const auto count=[&](const Raster& image){if(image.id.empty())return;bytes+=image.pixels.size();assets.insert(image.id);};
        for(const auto& [id,node]:nodes_) {
            count(node.image);
            for(const auto& o:node.overrides) if(o.patch.image) count(*o.patch.image);
            for(const auto& rule:node.definition.rules) if(rule.patch.image) count(*rule.patch.image);
        }
        if(bytes>kRasterMaxDocumentBytes || assets.size()>8) {apply(*inverse);return std::nullopt;}
    }
    return inverse;
}

Rect Document::box(const Id& id, const Id& mode) const {
    if (layoutDirty_ || layoutMode_ != mode) {
        layout_ = computeLayout(*this, mode);
        layoutMode_ = mode;
        layoutDirty_ = false;
    }
    const auto it = layout_.find(id);
    return it == layout_.end() ? Rect{} : it->second;
}

const Node* Document::find(const Id& id) const {
    const auto it = nodes_.find(id);
    return it == nodes_.end() ? nullptr : &it->second;
}

const std::vector<Id>& Document::children(const Id& parent) const {
    static const std::vector<Id> kNone;
    const auto it = children_.find(parent);
    return it == children_.end() ? kNone : it->second;
}

bool Document::isAncestorOrSelf(const Id& ancestor, const Id& id) const {
    for (const Node* node = find(id); node; node = find(node->parent)) {
        if (node->id == ancestor) {
            return true;
        }
    }
    return false;
}

std::optional<OpBody> Document::create(const CreateNode& op) {
    const Node& node = op.node;
    if(node.type!=NodeType::Image && (node.image!=Raster{} || node.imageFit!="cover" || node.imageCrop!=ImageCrop{})) return std::nullopt;
    if(node.type==NodeType::Image && !validRaster(node.image)) return std::nullopt;
    if (node.type != NodeType::Path && node.path != Path{}) return std::nullopt;
    if (node.type == NodeType::Path && !validPath(node.path)) return std::nullopt;
    if (node.type != NodeType::Line && node.lineReverse) return std::nullopt;
    if (node.type != NodeType::Line && node.type != NodeType::Path && node.lineWidth != 2.0) return std::nullopt;
    if ((node.type == NodeType::Line || node.type == NodeType::Path || node.type == NodeType::Image) && (!std::isfinite(node.lineWidth) || node.lineWidth < 0 || node.lineWidth > 4096 ||
        !validTextPaint(fullPatch(node)) || node.widthSizing != Sizing::Fixed || node.heightSizing != Sizing::Fixed || node.clipContent || node.limits != SizeLimits{})) return std::nullopt;
    if (node.type != NodeType::Text && (node.text != TextRun{} || node.textWrap || !node.typography.isNil())) return std::nullopt;
    if (node.type == NodeType::Text && !validTextPaint(fullPatch(node))) return std::nullopt;
    if (!node.definition.empty()) return std::nullopt; // Definitions are installed after the anatomy exists.
    if (!node.componentValues.empty() &&
        (node.type != NodeType::Instance || !find(node.component) ||
         !validComponentValues(find(node.component)->definition, node.componentValues))) return std::nullopt;
    if (node.id.isNil() || nodes_.contains(node.id) || !isValidParent(node.parent) ||
        !isValidPatch(fullPatch(node)) || !isValidNodeRefs(node)) {
        return std::nullopt;
    }
    if (node.type == NodeType::Instance && !isValidInstance(node)) {
        return std::nullopt;
    }
    for (const Override& o : node.overrides) {
        if (!isValidPatch(o.patch) || !isValidPatchRefs(o.patch) || o.patch.type || o.patch.component || o.patch.componentValues || o.patch.typography) return std::nullopt;
    }
    if (!node.overrides.empty()) {
        Document preview = *this;
        Node raw = node; raw.overrides.clear();
        if (!preview.apply(CreateNode{raw})) return std::nullopt;
        for (const auto& o : node.overrides) if (!preview.apply(SetOverride{node.id, o.node, o.patch})) return std::nullopt;
    }
    link(nodes_.emplace(node.id, node).first->second);
    return DeleteNode{node.id};
}

std::optional<OpBody> Document::remove(const DeleteNode& op) {
    if (definitionTargets(op.id)) return std::nullopt;
    const auto it = nodes_.find(op.id);
    if (it == nodes_.end() || !children(op.id).empty() ||
        (it->second.type == NodeType::Component && isComponentUsed(op.id))) {
        return std::nullopt;
    }
    CreateNode inverse{it->second};
    unlink(it->second);
    children_.erase(op.id);
    nodes_.erase(it);
    return inverse;
}

std::optional<OpBody> Document::setProps(const SetProps& op) {
    const auto it = nodes_.find(op.id);
    if (it == nodes_.end() || !isValidPatch(op.patch) || !isValidPatchRefs(op.patch)) {
        return std::nullopt;
    }
    const Node& current = it->second;
    if((op.patch.image || op.patch.imageFit || op.patch.imageCrop) && current.type!=NodeType::Image) return std::nullopt;
    if (op.patch.path && current.type != NodeType::Path) return std::nullopt;
    if (op.patch.lineReverse && current.type != NodeType::Line) return std::nullopt;
    if (op.patch.lineWidth && current.type != NodeType::Line && current.type != NodeType::Path) return std::nullopt;
    if ((current.type == NodeType::Line || current.type == NodeType::Path || current.type == NodeType::Image) && (!validTextPaint(op.patch) ||
        (op.patch.widthSizing && *op.patch.widthSizing != Sizing::Fixed) || (op.patch.heightSizing && *op.patch.heightSizing != Sizing::Fixed) ||
        (op.patch.clipContent && *op.patch.clipContent) || (op.patch.limits && *op.patch.limits != SizeLimits{}))) return std::nullopt;
    if (op.patch.typography && current.type != NodeType::Text) return std::nullopt;
    if (!current.typography.isNil() && (!op.patch.typography || !op.patch.typography->isNil())) {
        if (op.patch.textStyle) return std::nullopt;
        if (op.patch.text) {
            auto next=*op.patch.text; next.content=current.text.content;
            if (next != current.text) return std::nullopt;
        }
    }
    if (op.patch.text && current.type != NodeType::Text) return std::nullopt;
    if (op.patch.textStyle && current.type != NodeType::Text) return std::nullopt;
    if (op.patch.textWrap && current.type != NodeType::Text) return std::nullopt;
    if (op.patch.text && isTextTarget(op.id) && op.patch.text->content != current.text.content) return std::nullopt;
    if (current.type == NodeType::Text && !validTextPaint(op.patch)) return std::nullopt;
    if (op.patch.componentValues || op.patch.component) {
        const Id target = op.patch.component.value_or(current.component);
        const Node* main = find(target);
        const auto& values = op.patch.componentValues ? *op.patch.componentValues : current.componentValues;
        if ((!values.empty() || op.patch.componentValues) &&
            (current.type != NodeType::Instance || !main || !validComponentValues(main->definition, values))) return std::nullopt;
        if (isSwapTarget(op.id) && (op.patch.component || !values.empty())) return std::nullopt;
        if (main && (!op.patch.component || *op.patch.component == current.component) && swapValuesWouldStrandOverrides(current, values)) return std::nullopt;
    }
    if (op.patch.type && *op.patch.type != current.type) {
        const bool frameOrComponent = [](NodeType t) { return t == NodeType::Frame || t == NodeType::Component; }(
            *op.patch.type);
        if (!frameOrComponent || (current.type != NodeType::Frame && current.type != NodeType::Component)) {
            return std::nullopt;
        }
        if (current.type == NodeType::Component && (isComponentUsed(op.id) || !current.definition.empty())) {
            return std::nullopt;
        }
        if (*op.patch.type == NodeType::Component) {
            for (const auto& [id, main] : nodes_) {
                for (const auto& property : main.definition.properties)
                    if (isAncestorOrSelf(op.id, property.target)) return std::nullopt;
                for (const auto& rule : main.definition.rules)
                    if (isAncestorOrSelf(op.id, rule.target)) return std::nullopt;
                for (const auto& swap : main.definition.swaps)
                    if (isAncestorOrSelf(op.id, swap.target)) return std::nullopt;
                for (const auto& text : main.definition.texts)
                    if (isAncestorOrSelf(op.id, text.target)) return std::nullopt;
            }
        }
    }
    if (op.patch.component) {
        // A different anatomy cannot silently strand instance overrides. The editor must
        // explicitly reset them first; a future semantic mapping can preserve compatible ones.
        if (*op.patch.component != current.component && !current.overrides.empty()) return std::nullopt;
        Node candidate = current;
        candidate.component = *op.patch.component;
        if (current.type != NodeType::Instance || !isValidInstance(candidate)) return std::nullopt;
    }
    return SetProps{op.id, applyPatch(it->second, op.patch)};
}

std::optional<OpBody> Document::move(const MoveNode& op) {
    const auto it = nodes_.find(op.id);
    if (it == nodes_.end() || !isValidParent(op.parent)) {
        return std::nullopt;
    }
    if (!op.parent.isNil() && isAncestorOrSelf(op.id, op.parent)) {
        return std::nullopt;
    }
    bool cycle = false;
    if (op.parent != it->second.parent) {
        for (const auto& [id, main] : nodes_) {
            // Moving the whole main preserves its anatomy; reparenting a part within
            // the same main also keeps stable targets valid.
            if (isAncestorOrSelf(op.id, id) || enclosingComponent(op.parent) == id) continue;
            for (const auto& property : main.definition.properties)
                if (isAncestorOrSelf(op.id, property.target)) return std::nullopt;
            for (const auto& rule : main.definition.rules)
                if (isAncestorOrSelf(op.id, rule.target)) return std::nullopt;
            for (const auto& swap : main.definition.swaps)
                if (isAncestorOrSelf(op.id, swap.target)) return std::nullopt;
            for (const auto& text : main.definition.texts)
                if (isAncestorOrSelf(op.id, text.target)) return std::nullopt;
        }
    }
    const auto checkSubtree = [&](const Id& id, auto&& self) -> void {
        const Node& n = nodes_.at(id);
        if (n.type == NodeType::Instance && wouldCycle(op.parent, n.component)) cycle = true;
        if (n.type == NodeType::Component && !enclosingComponent(op.parent).isNil()) cycle = true;
        for (const Id& child : children(id)) self(child, self);
    };
    checkSubtree(op.id, checkSubtree);
    if (cycle) {
        return std::nullopt;
    }
    Node& node = it->second;
    MoveNode inverse{op.id, node.parent, node.order};
    unlink(node);
    node.parent = op.parent;
    node.order = op.order;
    link(node);
    return inverse;
}

void Document::link(const Node& node) {
    auto& siblings = children_[node.parent];
    const auto paintsBefore = [this](const Id& a, const Id& b) {
        const Node& na = nodes_.at(a);
        const Node& nb = nodes_.at(b);
        return na.order != nb.order ? na.order < nb.order : na.id < nb.id;
    };
    siblings.insert(std::upper_bound(siblings.begin(), siblings.end(), node.id, paintsBefore), node.id);
}

void Document::unlink(const Node& node) {
    const auto it = children_.find(node.parent);
    if (it == children_.end()) {
        return;
    }
    std::erase(it->second, node.id);
    if (it->second.empty()) {
        children_.erase(it);
    }
}

bool Document::isValidParent(const Id& parent) const {
    if (parent.isNil()) {
        return true;
    }
    const Node* node = find(parent);
    return node && canHaveChildren(node->type);
}

namespace {

constexpr int kMaxInstanceDepth = 16;

std::uint64_t mixBits(std::uint64_t x) {
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBull;
    x ^= x >> 31;
    return x;
}

/// A stable id for `node` copied inside `instance`.
Id copyKey(const Id& instance, const Id& node) {
    return {mixBits(instance.hi ^ mixBits(node.hi + 0x9E3779B97F4A7C15ull)),
            mixBits(instance.lo ^ mixBits(node.lo + 0xD1B54A32D192ED03ull))};
}

const NodePatch* findOverride(const std::vector<Override>& overrides, const Id& node) {
    for (const Override& o : overrides) {
        if (o.node == node) return &o.patch;
    }
    return nullptr;
}

} // namespace

std::optional<OpBody> Document::setOverride(const SetOverride& op) {
    const auto it = nodes_.find(op.instance);
    if (it == nodes_.end() || it->second.type != NodeType::Instance || !isValidPatch(op.patch) ||
        !isValidPatchRefs(op.patch) || op.patch.type || op.patch.component || op.patch.componentValues || op.patch.typography) {
        return std::nullopt;
    }
    if (isSwapTarget(op.instance) && op.node != it->second.component && op.patch != NodePatch{}) return std::nullopt;
    buildTree();
    const bool textTarget = std::any_of(tree_.begin(), tree_.end(), [&](const auto& entry) {
        return entry.second.owner == op.instance && entry.second.overrideKey == op.node && entry.second.node->type == NodeType::Text;
    });
    if ((op.patch.text && !textTarget) || (textTarget && !validTextPaint(op.patch))) return std::nullopt;
    if (op.patch.textStyle && !textTarget) return std::nullopt;
    const bool lineTarget = std::any_of(tree_.begin(),tree_.end(),[&](const auto& entry){return entry.second.owner==op.instance && entry.second.overrideKey==op.node && entry.second.node->type==NodeType::Line;});
    const bool pathTarget = std::any_of(tree_.begin(),tree_.end(),[&](const auto& entry){return entry.second.owner==op.instance && entry.second.overrideKey==op.node && entry.second.node->type==NodeType::Path;});
    const bool imageTarget = std::any_of(tree_.begin(),tree_.end(),[&](const auto& entry){return entry.second.owner==op.instance && entry.second.overrideKey==op.node && entry.second.node->type==NodeType::Image;});
    if((op.patch.image || op.patch.imageFit || op.patch.imageCrop) && !imageTarget) return std::nullopt;
    if (op.patch.path && !pathTarget) return std::nullopt;
    if (op.patch.lineReverse && !lineTarget) return std::nullopt;
    if (op.patch.lineWidth && !lineTarget && !pathTarget) return std::nullopt;
    if ((lineTarget || pathTarget || imageTarget) && (!validTextPaint(op.patch) || (op.patch.widthSizing && *op.patch.widthSizing != Sizing::Fixed) ||
        (op.patch.heightSizing && *op.patch.heightSizing != Sizing::Fixed) || (op.patch.clipContent && *op.patch.clipContent) ||
        (op.patch.limits && *op.patch.limits != SizeLimits{}))) return std::nullopt;
    if (op.patch.textWrap && !textTarget) return std::nullopt;
    if (op.patch.text && std::any_of(tree_.begin(), tree_.end(), [&](const auto& entry) {
        return entry.second.owner == op.instance && entry.second.overrideKey == op.node && isTextTarget(entry.second.source);
    })) return std::nullopt;
    auto& overrides = it->second.overrides;
    const auto existing = std::find_if(overrides.begin(), overrides.end(), [&](const Override& o) { return o.node == op.node; });
    SetOverride inverse{op.instance, op.node, existing == overrides.end() ? NodePatch{} : existing->patch};
    if (existing != overrides.end()) {
        overrides.erase(existing);
    }
    if (op.patch != NodePatch{}) {
        const auto at = std::lower_bound(overrides.begin(), overrides.end(), op.node,
                                         [](const Override& o, const Id& id) { return o.node < id; });
        overrides.insert(at, Override{op.node, op.patch});
    }
    return inverse;
}

bool Document::validComponentValues(const ComponentDefinition& definition, const ComponentValues& values) const {
    if (values.size() > 200) return false;
    for (const auto& [name, value] : values) {
        const auto textProperty = std::find_if(definition.texts.begin(), definition.texts.end(), [&](const auto& p) { return p.name == name; });
        if (textProperty != definition.texts.end()) {
            const auto* content = std::get_if<std::string>(&value);
            TextRun run; if (content) run.content = *content;
            if (!content || !validText(run)) return false;
            continue;
        }
        const auto property = std::find_if(definition.properties.begin(), definition.properties.end(),
                                           [&](const BooleanProperty& p) { return p.name == name; });
        if (property != definition.properties.end()) {
            if (!std::holds_alternative<bool>(value)) return false;
            continue;
        }
        const auto swap = std::find_if(definition.swaps.begin(), definition.swaps.end(), [&](const InstanceSwapProperty& p) { return p.name == name; });
        if (swap != definition.swaps.end()) {
            const auto* text = std::get_if<std::string>(&value);
            const auto selected = text ? Id::parse(*text) : std::nullopt;
            if (!selected || std::find(swap->choices.begin(), swap->choices.end(), *selected) == swap->choices.end()) return false;
            continue;
        }
        const auto axis = std::find_if(definition.axes.begin(), definition.axes.end(),
                                       [&](const VariantAxis& a) { return a.name == name; });
        const auto* selected = std::get_if<std::string>(&value);
        if (axis == definition.axes.end() || !selected ||
            std::find(axis->values.begin(), axis->values.end(), *selected) == axis->values.end()) return false;
    }
    return true;
}

bool Document::validDefinition(const Id& component, const ComponentDefinition& definition) const {
    if (definition.texts.size() > 64) return false;
    if (definition.properties.size() > 64 || definition.swaps.size() > 64 || definition.axes.size() > 8 || definition.rules.size() > 256) return false;
    const auto validName = [](const std::string& name) {
        return !name.empty() && name.size() <= 64 && name[0] >= 'a' && name[0] <= 'z' &&
            std::all_of(name.begin(), name.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; });
    };
    std::unordered_set<std::string> names;
    std::unordered_set<Id> targets;
    for (const auto& text : definition.texts) {
        TextRun run; run.content = text.defaultValue;
        const Node* target = find(text.target);
        if (!validName(text.name) || !names.insert(text.name).second || !targets.insert(text.target).second ||
            !target || target->type != NodeType::Text || enclosingComponent(text.target) != component || !validText(run)) return false;
        buildTree();
        for (const auto& [key, entry] : tree_) if (entry.source == text.target && entry.owner != key) {
            const Node* owner = find(entry.owner);
            if (owner && std::any_of(owner->overrides.begin(), owner->overrides.end(), [&](const Override& o) { return o.node == entry.overrideKey && o.patch.text.has_value(); })) return false;
        }
    }
    targets.clear();
    for (const auto& property : definition.properties) {
        if (!validName(property.name) || !names.insert(property.name).second ||
            !targets.insert(property.target).second || enclosingComponent(property.target) != component) return false;
    }
    for (const auto& axis : definition.axes) {
        if (!validName(axis.name) || !names.insert(axis.name).second || axis.values.empty() || axis.values.size() > 32 ||
            std::find(axis.values.begin(), axis.values.end(), axis.defaultValue) == axis.values.end()) return false;
        std::unordered_set<std::string> choices;
        for (const auto& value : axis.values) if (!validName(value) || !choices.insert(value).second) return false;
    }
    targets.clear();
    for (const auto& swap : definition.swaps) {
        const Node* target = find(swap.target);
        if (!validName(swap.name) || !names.insert(swap.name).second || !targets.insert(swap.target).second ||
            !target || target->type != NodeType::Instance || enclosingComponent(swap.target) != component ||
            !target->componentValues.empty() || std::any_of(target->overrides.begin(), target->overrides.end(), [&](const Override& o) { return o.node != target->component; }) ||
            swap.choices.empty() || swap.choices.size() > 32 || std::find(swap.choices.begin(), swap.choices.end(), swap.defaultComponent) == swap.choices.end()) return false;
        std::unordered_set<Id> choices;
        for (const Id& choice : swap.choices) {
            const Node* main = find(choice);
            if (!main || main->type != NodeType::Component || !choices.insert(choice).second || choice == component || componentUses(choice, component)) return false;
        }
    }
    for (const auto& rule : definition.rules) {
        const NodePatch& p = rule.patch;
        const bool lineTarget=find(rule.target) && find(rule.target)->type==NodeType::Line;
        const bool pathTarget=find(rule.target) && find(rule.target)->type==NodeType::Path;
        const bool imageTarget=find(rule.target) && find(rule.target)->type==NodeType::Image;
        if((p.image || p.imageFit || p.imageCrop) && !imageTarget) return false;
        if (p.path && !pathTarget) return false;
        if (p.lineReverse && !lineTarget) return false;
        if (p.lineWidth && !lineTarget && !pathTarget) return false;
        if ((lineTarget || pathTarget || imageTarget) && (!validTextPaint(p) || p.clipContent || p.limits)) return false;
        if (p.text && (!find(rule.target) || find(rule.target)->type != NodeType::Text)) return false;
        if (p.textStyle && (!find(rule.target) || find(rule.target)->type != NodeType::Text)) return false;
        if (p.textWrap && (!find(rule.target) || find(rule.target)->type != NodeType::Text)) return false;
        if (p.text && std::any_of(definition.texts.begin(), definition.texts.end(), [&](const auto& t) { return t.target == rule.target; })) return false;
        if (find(rule.target) && find(rule.target)->type == NodeType::Text && !validTextPaint(p)) return false;
        if (rule.when.empty() || enclosingComponent(rule.target) != component || !isValidPatch(p) || !isValidPatchRefs(p) ||
            p.type || p.component || p.componentValues || p.typography || p.name || p.visible || p.locked || p.absolute ||
            p.widthSizing || p.heightSizing || (rule.target == component && (p.x || p.y || p.w || p.h))) return false;
        for (const auto& [name, value] : rule.when) {
            const auto axis = std::find_if(definition.axes.begin(), definition.axes.end(), [&](const VariantAxis& a) { return a.name == name; });
            if (axis == definition.axes.end() || std::find(axis->values.begin(), axis->values.end(), value) == axis->values.end()) return false;
        }
    }
    return true;
}

bool Document::definitionTargets(const Id& id) const {
    for (const auto& [key, main] : nodes_) {
        if (key == id && !main.definition.empty()) return true;
        for (const auto& property : main.definition.properties) if (property.target == id) return true;
        for (const auto& rule : main.definition.rules) if (rule.target == id) return true;
        for (const auto& swap : main.definition.swaps) if (swap.target == id) return true;
        for (const auto& text : main.definition.texts) if (text.target == id) return true;
    }
    return false;
}

std::optional<OpBody> Document::setComponentDefinition(const SetComponentDefinition& op) {
    const auto it = nodes_.find(op.component);
    if (it == nodes_.end() || it->second.type != NodeType::Component || !validDefinition(op.component, op.definition)) return std::nullopt;
    if (it->second.definition.swaps != op.definition.swaps && hasDependentOverrides(op.component)) return std::nullopt;
    for (const auto& [id, node] : nodes_)
        if (node.type == NodeType::Instance && node.component == op.component && !validComponentValues(op.definition, node.componentValues)) return std::nullopt;
    SetComponentDefinition inverse{op.component, it->second.definition};
    it->second.definition = op.definition;
    return inverse;
}

Id Document::enclosingComponent(const Id& id) const {
    for (const Node* node = find(id); node; node = find(node->parent)) {
        if (node->type == NodeType::Component) return node->id;
    }
    return {};
}

bool Document::isTextTarget(const Id& id) const {
    const Node* main = find(enclosingComponent(id));
    return main && std::any_of(main->definition.texts.begin(), main->definition.texts.end(), [&](const auto& t) { return t.target == id; });
}

bool Document::componentUses(const Id& component, const Id& target) const {
    std::unordered_set<Id> seen;
    const auto visit = [&](const Id& id, auto&& self) -> bool {
        const Node* node = find(id);
        if (!node) return false;
        if (node->type == NodeType::Instance) {
            if (node->component == target) return true;
            if (seen.insert(node->component).second && self(node->component, self)) return true;
        }
        for (const auto& swap : node->definition.swaps) for (const Id& choice : swap.choices) {
            if (choice == target) return true;
            if (seen.insert(choice).second && self(choice, self)) return true;
        }
        for (const Id& child : children(id)) {
            if (self(child, self)) return true;
        }
        return false;
    };
    return visit(component, visit);
}

bool Document::wouldCycle(const Id& parent, const Id& used) const {
    const Id host = enclosingComponent(parent);
    return !host.isNil() && (host == used || componentUses(used, host));
}

bool Document::isValidInstance(const Node& node) const {
    const Node* component = find(node.component);
    return component && component->type == NodeType::Component && !wouldCycle(node.parent, node.component);
}

bool Document::isComponentUsed(const Id& component) const {
    for (const auto& [id, node] : nodes_) {
        if (node.type == NodeType::Instance && node.component == component) return true;
        for (const auto& swap : node.definition.swaps)
            if (std::find(swap.choices.begin(), swap.choices.end(), component) != swap.choices.end()) return true;
    }
    return false;
}

bool Document::isSwapTarget(const Id& id) const {
    for (const auto& [key, node] : nodes_)
        for (const auto& swap : node.definition.swaps) if (swap.target == id) return true;
    return false;
}

bool Document::hasDependentOverrides(const Id& component) const {
    if (component.isNil()) return false;
    for (const auto& [id, node] : nodes_) if (!node.overrides.empty() &&
        (node.component == component || componentUses(node.component, component))) return true;
    return false;
}

bool Document::swapValuesWouldStrandOverrides(const Node& instance, const ComponentValues& values) const {
    const Node* main = find(instance.component);
    if (!main || main->definition.swaps.empty()) return false;
    std::unordered_set<Id> changedTargets;
    for (const auto& swap : main->definition.swaps) {
        const auto selected = [&](const ComponentValues& choices) {
            const auto found = choices.find(swap.name);
            return found == choices.end() ? swap.defaultComponent.toString() : std::get<std::string>(found->second);
        };
        if (selected(instance.componentValues) != selected(values)) changedTargets.insert(swap.target);
    }
    if (changedTargets.empty()) return false;
    // An edit to an instance in a main changes all copies of that main. Deeper override
    // correspondence is not inferred; require an explicit reset before that global edit.
    if (hasDependentOverrides(enclosingComponent(instance.parent))) return true;
    if (instance.overrides.empty()) return false;
    buildTree();
    std::unordered_set<Id> knownKeys{instance.component};
    const auto visit = [&](const Id& key, bool affected, auto&& self) -> bool {
        const bool wrapper = changedTargets.contains(sourceOf(key));
        affected = affected || wrapper;
        const Id address = overrideKeyOf(key);
        if (!address.isNil()) knownKeys.insert(address);
        // The slot wrapper keeps its identity and common root fields. Descendants
        // belong to the chosen anatomy and cannot be remapped implicitly.
        if (affected && !wrapper) for (const auto& o : instance.overrides) if (o.node == address) return true;
        for (const auto& child : viewChildren(key)) if (self(child, affected, self)) return true;
        return false;
    };
    if (visit(instance.id, false, visit)) return true;
    return std::any_of(instance.overrides.begin(), instance.overrides.end(), [&](const Override& o) { return !knownKeys.contains(o.node); });
}

const Node* Document::view(const Id& key) const {
    buildTree();
    const auto it = tree_.find(key);
    return it == tree_.end() ? nullptr : it->second.node;
}

const std::vector<Id>& Document::viewChildren(const Id& key) const {
    static const std::vector<Id> kNone;
    buildTree();
    const auto it = treeChildren_.find(key);
    return it == treeChildren_.end() ? kNone : it->second;
}

Id Document::ownerOf(const Id& key) const {
    buildTree();
    const auto it = tree_.find(key);
    return it == tree_.end() ? Id{} : it->second.owner;
}

Id Document::overrideKeyOf(const Id& key) const {
    buildTree();
    const auto it = tree_.find(key);
    return it == tree_.end() ? Id{} : it->second.overrideKey;
}

Id Document::sourceOf(const Id& key) const {
    buildTree();
    const auto it = tree_.find(key);
    return it == tree_.end() ? Id{} : it->second.source;
}

void Document::buildTree() const {
    if (!treeDirty_) {
        return;
    }
    tree_.clear();
    treeChildren_.clear();
    treeNodes_.clear();
    for (const Id& id : children(Id{})) expand(id, id, {}, {}, {}, 0);
    treeDirty_ = false;
}

void Document::expand(const Id& source, const Id& key, const Id& parent, const Id& owner,
                      const std::vector<OverrideLayer>& layers, int depth) const {
    const Node& original = nodes_.at(source);
    const Id expansionOwner = owner.isNil() && original.type == NodeType::Instance ? key : owner;
    const auto keyIn = [&](const OverrideLayer& layer) {
        return layer.prefix.isNil() ? source : copyKey(layer.prefix, source);
    };
    const auto applyDefinition = [&](Node& node, const Id& target, const OverrideLayer& layer) {
        if (!layer.definition) return;
        const auto valueOf = [&](const std::string& name) -> const ComponentValue* {
            if (!layer.values) return nullptr;
            const auto found = layer.values->find(name);
            return found == layer.values->end() ? nullptr : &found->second;
        };
        for (const auto& property : layer.definition->properties) if (property.target == target) {
            const auto* value = valueOf(property.name);
            node.visible = value ? std::get<bool>(*value) : property.defaultValue;
        }
        for (const auto& swap : layer.definition->swaps) if (swap.target == target) {
            const auto* value = valueOf(swap.name);
            node.component = value ? *Id::parse(std::get<std::string>(*value)) : swap.defaultComponent;
        }
        std::vector<const VariantRule*> matches;
        for (const auto& rule : layer.definition->rules) {
            if (rule.target != target) continue;
            bool matchesCell = true;
            for (const auto& [name, expected] : rule.when) {
                const auto* value = valueOf(name);
                const auto axis = std::find_if(layer.definition->axes.begin(), layer.definition->axes.end(), [&](const VariantAxis& a) { return a.name == name; });
                matchesCell = matchesCell && (value ? std::get<std::string>(*value) : axis->defaultValue) == expected;
            }
            if (matchesCell) matches.push_back(&rule);
        }
        std::stable_sort(matches.begin(), matches.end(), [](const VariantRule* a, const VariantRule* b) { return a->when.size() < b->when.size(); });
        for (const auto* rule : matches) applyPatch(node, rule->patch);
        for (const auto& text : layer.definition->texts) if (text.target == target) {
            const auto* value = valueOf(text.name);
            node.text.content = value ? std::get<std::string>(*value) : text.defaultValue;
        }
    };
    const auto applyLayers = [&](Node& node) {
        for (const OverrideLayer& layer : layers) {
            applyDefinition(node, source, layer);
            if (layer.overrides) if (const NodePatch* patch = findOverride(*layer.overrides, keyIn(layer))) applyPatch(node, *patch);
        }
    };

    Node effective = original;
    if (!original.typography.isNil()) {
        const auto value=resolveToken(original.typography,kDefaultModeId);
        if (value) if (const auto* style=std::get_if<TextStyle>(&*value)) {
            NodePatch patch; patch.textStyle=*style; applyPatch(effective,patch);
        }
    }
    std::vector<OverrideLayer> inner = layers;
    Id childrenOf = source;
    Node reference = original;
    if (original.type == NodeType::Instance) applyLayers(reference);
    const Node* component = original.type == NodeType::Instance ? find(reference.component) : nullptr;
    if (component && depth < kMaxInstanceDepth) {
        effective = *component;
        applyDefinition(effective, component->id, {nullptr, {}, &component->definition, &original.componentValues});
        if (const NodePatch* patch = findOverride(original.overrides, original.component)) applyPatch(effective, *patch);
        effective.x = original.x;
        effective.y = original.y;
        effective.w = original.w;
        effective.h = original.h;
        effective.widthSizing = original.widthSizing;
        effective.heightSizing = original.heightSizing;
        effective.limits = original.limits;
        effective.absolute = original.absolute;
        effective.visible = original.visible && effective.visible;
        effective.locked = original.locked;
        effective.name = original.name;
        effective.order = original.order;
        applyLayers(effective);
        inner = {{&original.overrides, {}, &component->definition, &original.componentValues}};
        for (const OverrideLayer& layer : layers) inner.push_back({layer.overrides, keyIn(layer), layer.definition, layer.values});
        childrenOf = component->id;
    } else {
        if (original.type == NodeType::Component) {
            applyDefinition(effective, original.id, {nullptr, {}, &original.definition, nullptr});
            inner.insert(inner.begin(), {nullptr, {}, &original.definition, nullptr});
        }
        applyLayers(effective);
        if (original.type == NodeType::Instance) childrenOf = Id{};
    }
    if (original.type == NodeType::Instance) {
        effective.type = NodeType::Instance;
        effective.component = reference.component;
    }
    effective.id = key;
    effective.parent = parent;
    treeNodes_.push_back(std::move(effective));
    const auto addressing = std::find_if(layers.rbegin(), layers.rend(), [](const OverrideLayer& layer) { return layer.overrides != nullptr; });
    tree_[key] = {&treeNodes_.back(), source, expansionOwner.isNil() ? key : expansionOwner,
                  expansionOwner.isNil() || addressing == layers.rend() ? Id{} : keyIn(*addressing)};
    treeChildren_[parent].push_back(key);

    if (childrenOf.isNil()) {
        return;
    }
    for (const Id& child : children(childrenOf)) {
        expand(child, expansionOwner.isNil() ? child : copyKey(key, child), key, expansionOwner, inner, depth + 1);
    }
}

} // namespace vc
