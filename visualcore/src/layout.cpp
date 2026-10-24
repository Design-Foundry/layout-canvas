#include "vc/layout.hpp"

#include <algorithm>
#include <optional>
#include <vector>
#include "vc/document.hpp"

namespace vc {
namespace {

struct Size {
    double w = 0.0;
    double h = 0.0;
};

class Engine {
public:
    Engine(const Document& document, const Id& mode) : document_(document), mode_(mode) {}

    std::unordered_map<Id, Rect> run() {
        for (const Id& id : document_.viewChildren(Id{})) {
            const Node& node = *document_.view(id);
            const Size size = measure(node);
            place(node, {node.x, node.y, size.w, size.h});
        }
        return std::move(boxes_);
    }

private:
    double length(const Value<double>& value) const {
        return document_.resolveLength(value, mode_).value_or(0.0);
    }

    double clampWidth(const Node& node, double w) const {
        if (node.limits.maxWidth) w = std::min(w, length(*node.limits.maxWidth));
        if (node.limits.minWidth) w = std::max(w, length(*node.limits.minWidth));
        return std::max(w, 0.0);
    }

    double clampHeight(const Node& node, double h) const {
        if (node.limits.maxHeight) h = std::min(h, length(*node.limits.maxHeight));
        if (node.limits.minHeight) h = std::max(h, length(*node.limits.minHeight));
        return std::max(h, 0.0);
    }

    static bool hasAutoLayout(const Node& node) {
        return isContainer(node.type) && node.layout.type != LayoutType::None;
    }

    std::vector<const Node*> flowChildren(const Node& node) const {
        std::vector<const Node*> out;
        for (const Id& id : document_.viewChildren(node.id)) {
            const Node* child = document_.view(id);
            if (child->visible && !child->absolute) out.push_back(child);
        }
        return out;
    }

    /// The size a node wants before its parent stretches or fills it.
    Size measure(const Node& node, double availableWidth = -1) {
        Size size{node.w, node.h};
        if (availableWidth >= 0) size.w = availableWidth;
        if (node.type == NodeType::Text) {
            const auto text = layoutText(node.text);
            if (node.widthSizing == Sizing::Hug) size.w = text.width;
            if (availableWidth >= 0) size.w = availableWidth;
            size.w = clampWidth(node, size.w);
            if (node.heightSizing == Sizing::Hug) size.h = node.textWrap ? layoutText(node.text, size.w, true).height : text.height;
        }
        if (hasAutoLayout(node) && (node.widthSizing == Sizing::Hug || node.heightSizing == Sizing::Hug)) {
            const bool row = node.layout.type == LayoutType::Row;
            const auto& pad = node.layout.padding;
            const double gap = length(node.layout.gap);
            double main = 0.0;
            double cross = 0.0;
            const auto children = flowChildren(node);
            const double innerWidth = std::max(0.0, clampWidth(node, size.w) - length(pad[kStart]) - length(pad[kEnd]));
            double fillWidth = -1;
            const bool constrainedWidth = availableWidth >= 0 || node.widthSizing != Sizing::Hug;
            std::vector<std::optional<Size>> measured(children.size());
            if (row && constrainedWidth) {
                double occupied = children.size() > 1 ? gap * (children.size() - 1) : 0;
                std::size_t fills = 0;
                for (std::size_t i = 0; i < children.size(); ++i) {
                    if (children[i]->widthSizing == Sizing::Fill) ++fills;
                    else occupied += (measured[i] = measure(*children[i]))->w;
                }
                if (fills) fillWidth = std::max(0.0, (innerWidth - occupied) / fills);
            }
            for (std::size_t i = 0; i < children.size(); ++i) {
                const Node* child = children[i];
                const bool constrained = constrainedWidth &&
                    (row ? child->widthSizing == Sizing::Fill : child->widthSizing == Sizing::Fill || node.layout.align == Align::Stretch);
                const Size childSize = constrained ? measure(*child, row ? fillWidth : innerWidth)
                    : measured[i] ? *measured[i] : measure(*child);
                main += row ? childSize.w : childSize.h;
                cross = std::max(cross, row ? childSize.h : childSize.w);
            }
            if (children.size() > 1) main += gap * static_cast<double>(children.size() - 1);
            const double hugW = (row ? main : cross) + length(pad[kStart]) + length(pad[kEnd]);
            const double hugH = (row ? cross : main) + length(pad[kTop]) + length(pad[kBottom]);
            if (node.widthSizing == Sizing::Hug && availableWidth < 0) {
                size.w = hugW;
                if (clampWidth(node, hugW) != hugW) return measure(node, clampWidth(node, hugW));
            }
            if (node.heightSizing == Sizing::Hug) size.h = hugH;
        }
        return {clampWidth(node, size.w), clampHeight(node, size.h)};
    }

    void place(const Node& node, const Rect& box) {
        boxes_[node.id] = box;
        if (!isContainer(node.type)) {
            return;
        }
        if (!hasAutoLayout(node)) {
            for (const Id& id : document_.viewChildren(node.id)) {
                const Node& child = *document_.view(id);
                const Size size = measure(child);
                place(child, {child.x, child.y, size.w, size.h});
            }
            return;
        }
        arrange(node, box);
    }

    void arrange(const Node& node, const Rect& box) {
        const bool row = node.layout.type == LayoutType::Row;
        const auto& pad = node.layout.padding;
        const double padStart = length(pad[kStart]);
        const double padTop = length(pad[kTop]);
        const double innerW = std::max(0.0, box.w - padStart - length(pad[kEnd]));
        const double innerH = std::max(0.0, box.h - padTop - length(pad[kBottom]));
        const double innerMain = row ? innerW : innerH;
        const double innerCross = row ? innerH : innerW;
        const double gap = length(node.layout.gap);

        const auto children = flowChildren(node);
        struct Item {
            const Node* node;
            double main;
            double cross;
        };
        std::vector<Item> items;
        double fixedMain = 0.0;
        int fills = 0;
        for (const Node* child : children) {
            const Sizing mainSizing = row ? child->widthSizing : child->heightSizing;
            const Sizing crossSizing = row ? child->heightSizing : child->widthSizing;
            const Size size = measure(*child, !row && (crossSizing == Sizing::Fill || node.layout.align == Align::Stretch) ? clampWidth(*child, innerW) : -1);
            Item item{child, row ? size.w : size.h, row ? size.h : size.w};
            if (mainSizing == Sizing::Fill) {
                ++fills;
            } else {
                fixedMain += item.main;
            }
            if (crossSizing == Sizing::Fill || node.layout.align == Align::Stretch) {
                item.cross = row ? clampHeight(*child, innerCross) : clampWidth(*child, innerCross);
            }
            items.push_back(item);
        }

        const double gaps = items.size() > 1 ? gap * static_cast<double>(items.size() - 1) : 0.0;
        if (fills > 0) {
            const double share = std::max(0.0, (innerMain - fixedMain - gaps) / fills);
            fixedMain = 0.0;
            for (Item& item : items) {
                const Sizing mainSizing = row ? item.node->widthSizing : item.node->heightSizing;
                if (mainSizing == Sizing::Fill) {
                    item.main = row ? clampWidth(*item.node, share) : clampHeight(*item.node, share);
                }
                fixedMain += item.main;
            }
        }

        if (row) for (Item& item : items) {
            if (item.node->widthSizing == Sizing::Fill && item.node->heightSizing == Sizing::Hug && node.layout.align != Align::Stretch)
                item.cross = measure(*item.node, item.main).h;
        }
        const double free = innerMain - fixedMain - gaps;
        double offset = 0.0;
        double spacing = gap;
        const auto n = static_cast<double>(items.size());
        switch (node.layout.justify) {
        case Justify::Start:
            break;
        case Justify::Center:
            offset = free / 2.0;
            break;
        case Justify::End:
            offset = free;
            break;
        case Justify::SpaceBetween:
            if (items.size() > 1) spacing = (innerMain - fixedMain) / (n - 1.0);
            break;
        case Justify::SpaceAround:
            if (!items.empty()) {
                spacing = (innerMain - fixedMain) / n;
                offset = spacing / 2.0;
            }
            break;
        case Justify::SpaceEvenly:
            if (!items.empty()) {
                spacing = (innerMain - fixedMain) / (n + 1.0);
                offset = spacing;
            }
            break;
        }

        double cursor = offset;
        for (const Item& item : items) {
            double crossOffset = 0.0;
            switch (node.layout.align) {
            case Align::Center:
                crossOffset = (innerCross - item.cross) / 2.0;
                break;
            case Align::End:
                crossOffset = innerCross - item.cross;
                break;
            default:
                break;
            }
            const Rect childBox = row ? Rect{padStart + cursor, padTop + crossOffset, item.main, item.cross}
                                      : Rect{padStart + crossOffset, padTop + cursor, item.cross, item.main};
            place(*item.node, childBox);
            cursor += item.main + spacing;
        }

        for (const Id& id : document_.viewChildren(node.id)) {
            const Node& child = *document_.view(id);
            if (child.absolute || !child.visible) {
                const Size size = measure(child);
                place(child, {child.x, child.y, size.w, size.h});
            }
        }
    }

    const Document& document_;
    Id mode_;
    std::unordered_map<Id, Rect> boxes_;
};

} // namespace

std::unordered_map<Id, Rect> computeLayout(const Document& document, const Id& mode) {
    return Engine{document, mode}.run();
}

} // namespace vc
