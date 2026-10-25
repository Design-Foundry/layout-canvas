#include "vc/draw_list.hpp"
#include "vc/geometry.hpp"

#include <algorithm>

namespace vc {
void transformDrawItem(DrawItem& item, Vec2 offset, double opacity, std::optional<Rect> viewport) {
    const auto translate=[&](Rect& r) {r.x+=offset.x;r.y+=offset.y;};
    translate(item.quad);translate(item.clip);
    if(viewport) {
        const auto x=std::max(item.clip.x,viewport->x),y=std::max(item.clip.y,viewport->y);
        item.clip={x,y,std::max(0.,std::min(item.clip.x+item.clip.w,viewport->x+viewport->w)-x),
                       std::max(0.,std::min(item.clip.y+item.clip.h,viewport->y+viewport->h)-y)};
    }
    if(item.kind!=DrawKind::Glyph && item.kind!=DrawKind::Image) translate(item.a.rect);
    translate(item.b.rect);
    for(auto& point:item.points) {point.x+=offset.x;point.y+=offset.y;}
    item.color.a*=static_cast<float>(std::clamp(opacity,0.,1.));
}
namespace {

constexpr double kUnbounded = 1e30;
constexpr double kSigmaReach = 3.0;

Rect intersect(const Rect& a, const Rect& b) {
    const double x0 = std::max(a.x, b.x);
    const double y0 = std::max(a.y, b.y);
    const double x1 = std::min(a.x + a.w, b.x + b.w);
    const double y1 = std::min(a.y + a.h, b.y + b.h);
    return {x0, y0, std::max(0.0, x1 - x0), std::max(0.0, y1 - y0)};
}

/// Grows `r` by `top`, `end`, `bottom`, `start` on each side (negative shrinks).
Rect grow(const Rect& r, double top, double end, double bottom, double start) {
    return {r.x - start, r.y - top, std::max(0.0, r.w + start + end), std::max(0.0, r.h + top + bottom)};
}

Rect grow(const Rect& r, double all) {
    return grow(r, all, all, all, all);
}

Rect offset(const Rect& r, double dx, double dy) {
    return {r.x + dx, r.y + dy, r.w, r.h};
}

std::array<double, 4> adjust(const std::array<double, 4>& radii, double amount) {
    std::array<double, 4> out{};
    for (std::size_t i = 0; i < 4; ++i) {
        out[i] = radii[i] > 0.0 ? std::max(0.0, radii[i] + amount) : 0.0;
    }
    return out;
}

Color withAlpha(const Oklch& color, double opacity) {
    Color srgb = toSrgb(color);
    srgb.a = static_cast<float>(srgb.a * opacity);
    return srgb;
}

class Builder {
public:
    Builder(const Document& document, const Rect& view, const Id& mode, std::vector<DrawItem>& out,const Id& root)
        : document_(document), view_(view), mode_(mode), out_(out),root_(root) {}

    std::size_t run() {
        out_.clear();
        Rect clip{-kUnbounded,-kUnbounded,2*kUnbounded,2*kUnbounded};
        if(!root_.isNil()) {
            const auto* node=document_.find(root_);
            const auto bounds=worldBounds(document_,root_,mode_);
            if(!node || !node->parent.isNil() || !node->visible || !bounds) return 0;
            clip=*bounds;
        }
        collect(Id{}, {}, clip, 1.0);
        return drawn_;
    }

private:
    void collect(const Id& parent, Vec2 origin, const Rect& clip, double opacity) {
        const Rect visibleArea = intersect(view_, clip);
        for (const Id& id : document_.viewChildren(parent)) {
            if(parent.isNil() && !root_.isNil() && id!=root_) continue;
            const Node& node = *document_.view(id);
            if (!node.visible) {
                continue;
            }
            const Rect local = document_.box(id, mode_);
            const Rect box{origin.x + local.x, origin.y + local.y, local.w, local.h};
            const double alpha = opacity * document_.resolveOpacity(node.opacity, mode_);
            if(node.type==NodeType::Image) {
                if(node.fillVisible && box.w>0 && box.h>0 && box.intersects(visibleArea)) {
                    auto uv=Rect{node.imageCrop.x,node.imageCrop.y,node.imageCrop.w,node.imageCrop.h};auto quad=box;
                    const double sourceW=uv.w*node.image.width,sourceH=uv.h*node.image.height;
                    if(node.imageFit=="contain") {
                        const auto scale=std::min(box.w/sourceW,box.h/sourceH);
                        quad={box.x+(box.w-sourceW*scale)/2,box.y+(box.h-sourceH*scale)/2,sourceW*scale,sourceH*scale};
                    } else if(node.imageFit=="cover") {
                        const auto scale=std::max(box.w/sourceW,box.h/sourceH);
                        const double width=box.w/scale/node.image.width,height=box.h/scale/node.image.height;
                        uv={uv.x+(uv.w-width)/2,uv.y+(uv.h-height)/2,width,height};
                    }
                    DrawItem item;item.kind=DrawKind::Image;item.quad=quad;item.a.rect=uv;item.color={1,1,1,float(alpha)};item.clip=intersect(clip,box);item.image=&node.image;
                    out_.push_back(std::move(item));++drawn_;
                }
                continue;
            }
            if (node.type == NodeType::Path) {
                const auto quad=grow(box,node.lineWidth/2);
                if (node.fillVisible && (node.lineWidth>0 || node.path.filled) && quad.intersects(visibleArea)) {
                    const auto fill=document_.resolveColor(node.fill,mode_);
                    auto color=fill?withAlpha(*fill,alpha):kUnresolvedColor; if(!fill) color.a=float(alpha);
                    DrawItem item; item.kind=DrawKind::Path;item.quad=quad;item.color=color;item.clip=clip;
                    item.strokeWidth=node.lineWidth;item.points=flattenPath(node,box);item.closed=node.path.closed;item.filled=node.path.filled;
                    out_.push_back(std::move(item));++drawn_;
                }
                continue;
            }
            if (node.type == NodeType::Line) {
                const auto quad=grow(box,node.lineWidth/2);
                if (node.fillVisible && (node.lineWidth>0 || node.path.filled) && quad.intersects(visibleArea)) {
                    const auto points=lineEndpoints(node,box);
                    const auto fill=document_.resolveColor(node.fill,mode_);
                    auto color=fill?withAlpha(*fill,alpha):kUnresolvedColor; if(!fill) color.a=float(alpha);
                    out_.push_back({DrawKind::Segment,false,quad,{{points[0].x,points[0].y,0,0},{}},{{points[1].x,points[1].y,0,0},{}},color,clip,0,StrokeStyle::Solid,node.lineWidth});
                    ++drawn_;
                }
                continue;
            }
            if (node.type == NodeType::Text) {
                const auto text = layoutText(node.text, box.w, node.textWrap);
                const auto fill = document_.resolveColor(node.fill, mode_);
                Color color = fill ? withAlpha(*fill, alpha) : kUnresolvedColor;
                if (!fill) color.a = static_cast<float>(alpha);
                const auto textClip = intersect(clip, box);
                bool visible = false;
                if (node.fillVisible) for (const auto& glyph : text.glyphs) {
                    const Rect quad = offset(glyph.box, box.x, box.y);
                    if (!quad.intersects(intersect(view_, textClip))) continue;
                    // Glyph items use a.rect for normalized atlas coordinates.
                    out_.push_back({DrawKind::Glyph, false, quad, {glyph.uv, {}}, {}, color, textClip});
                    visible = true;
                }
                if (visible) ++drawn_;
                continue;
            }
            const bool ellipse = node.type == NodeType::Ellipse;
            auto radii = ellipse ? std::array<double, 4>{} : document_.resolveCorners(node, mode_);
            const double maxRadius = std::min(box.w, box.h) / 2.0;
            for (double& r : radii) r = std::min(r, maxRadius);

            const auto shadows = document_.resolveShadows(node.shadows, mode_);
            const auto stroke = document_.resolveStroke(node.stroke, mode_);
            const Rect reach = grow(box, extent(shadows, stroke));
            const bool inView = reach.intersects(visibleArea);
            const std::size_t before = out_.size();

            if (inView) {
                for (auto it = shadows.rbegin(); it != shadows.rend(); ++it) {
                    if (!it->inset) dropShadow(*it, box, radii, ellipse, clip, alpha);
                }
                if (node.fillVisible) {
                    const auto fill = document_.resolveColor(node.fill, mode_);
                    Color color = fill ? withAlpha(*fill, alpha) : kUnresolvedColor;
                    if (!fill) color.a = static_cast<float>(alpha);
                    out_.push_back({DrawKind::Fill, ellipse, box, {box, radii}, {}, color, clip});
                }
                for (auto it = shadows.rbegin(); it != shadows.rend(); ++it) {
                    if (it->inset) innerShadow(*it, box, radii, ellipse, clip, alpha);
                }
            }

            if (isContainer(node.type)) {
                const Rect childClip = node.clipContent ? intersect(clip, box) : clip;
                if (!node.clipContent || childClip.intersects(view_)) {
                    collect(id, {box.x, box.y}, childClip, alpha);
                }
            }

            if (inView && stroke) {
                strokeItem(*stroke, box, radii, ellipse, clip, alpha);
            }
            if (out_.size() > before) {
                ++drawn_;
            }
        }
    }

    static double extent(const std::vector<ResolvedShadow>& shadows, const std::optional<ResolvedStroke>& stroke) {
        double reach = 0.0;
        for (const ResolvedShadow& s : shadows) {
            if (!s.inset) {
                reach = std::max(reach, std::max(std::abs(s.x), std::abs(s.y)) + s.spread + kSigmaReach * s.blur / 2.0);
            }
        }
        if (stroke) {
            const double widest = *std::max_element(stroke->widths.begin(), stroke->widths.end());
            reach = std::max(reach, stroke->align == StrokeAlign::Inside ? 0.0
                                    : stroke->align == StrokeAlign::Center ? widest / 2.0
                                                                           : widest);
        }
        return reach;
    }

    void dropShadow(const ResolvedShadow& s, const Rect& box, const std::array<double, 4>& radii, bool ellipse,
                    const Rect& clip, double alpha) {
        const Rect shape = grow(offset(box, s.x, s.y), s.spread);
        const double sigma = s.blur / 2.0;
        DrawItem item{DrawKind::DropShadow, ellipse, grow(shape, kSigmaReach * sigma), {shape, adjust(radii, s.spread)},
                      {}, withAlpha(s.color, alpha), clip, sigma};
        out_.push_back(item);
    }

    void innerShadow(const ResolvedShadow& s, const Rect& box, const std::array<double, 4>& radii, bool ellipse,
                     const Rect& clip, double alpha) {
        const Rect hole = grow(offset(box, s.x, s.y), -s.spread);
        DrawItem item{DrawKind::InnerShadow, ellipse, box, {hole, adjust(radii, -s.spread)}, {box, radii},
                      withAlpha(s.color, alpha), clip, s.blur / 2.0};
        out_.push_back(item);
    }

    void strokeItem(const ResolvedStroke& stroke, const Rect& box, const std::array<double, 4>& radii, bool ellipse,
                    const Rect& clip, double alpha) {
        const auto& w = stroke.widths;
        const double factor = stroke.align == StrokeAlign::Inside ? 0.0 : stroke.align == StrokeAlign::Center ? 0.5 : 1.0;
        const Rect outer = grow(box, w[kTop] * factor, w[kEnd] * factor, w[kBottom] * factor, w[kStart] * factor);
        const double widest = *std::max_element(w.begin(), w.end());
        const auto outerRadii = adjust(radii, widest * factor);
        const Rect inner = grow(outer, -w[kTop], -w[kEnd], -w[kBottom], -w[kStart]);
        const auto innerRadii = adjust(outerRadii, -widest);
        DrawItem item{DrawKind::Stroke, ellipse, outer, {outer, outerRadii}, {inner, innerRadii},
                      withAlpha(stroke.color, alpha), clip, 0.0, stroke.style, widest};
        out_.push_back(item);
    }

    const Document& document_;
    Rect view_;
    Id mode_;
    std::vector<DrawItem>& out_;
    std::size_t drawn_ = 0;
    Id root_;
};

} // namespace

std::size_t buildDrawList(const Document& document, const Rect& view, const Id& mode, std::vector<DrawItem>& out,const Id& root) {
    return Builder{document, view, mode, out,root}.run();
}

} // namespace vc
