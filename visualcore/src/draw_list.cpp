#include "vc/draw_list.hpp"
#include "vc/geometry.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <map>

namespace vc {
void transformDrawItem(DrawItem& item, Vec2 offset, double opacity, std::optional<Rect> viewport, double scale) {
    const auto translate=[&](Rect& r) {r.x=r.x*scale+offset.x;r.y=r.y*scale+offset.y;r.w*=scale;r.h*=scale;};
    translate(item.quad);translate(item.clip);
    if(viewport) {
        const auto x=std::max(item.clip.x,viewport->x),y=std::max(item.clip.y,viewport->y);
        item.clip={x,y,std::max(0.,std::min(item.clip.x+item.clip.w,viewport->x+viewport->w)-x),
                       std::max(0.,std::min(item.clip.y+item.clip.h,viewport->y+viewport->h)-y)};
    }
    if(item.kind!=DrawKind::Glyph && item.kind!=DrawKind::Image) translate(item.a.rect);
    translate(item.b.rect);
    for(auto& point:item.points) {point.x=point.x*scale+offset.x;point.y=point.y*scale+offset.y;}
    for(auto* radii:{&item.a.radii,&item.b.radii}) for(auto& radius:*radii) radius*=scale;
    item.sigma*=scale;item.strokeWidth*=scale;
    item.color.a*=static_cast<float>(std::clamp(opacity,0.,1.));
}

std::optional<std::vector<DrawItem>> interpolateDrawLists(
    const std::vector<DrawItem>& from,const std::vector<DrawItem>& to,double progress) {
    if(!std::isfinite(progress) || from.size()!=to.size()) return std::nullopt;
    const auto sameRect=[](const Rect& a,const Rect& b) {return a.x==b.x && a.y==b.y && a.w==b.w && a.h==b.h;};
    std::set<std::pair<Id,std::uint32_t>> identities;
    for(std::size_t i=0;i<from.size();++i) {
        const auto& a=from[i];const auto& b=to[i];
        if(a.node.isNil() || a.node!=b.node || a.primitive!=b.primitive ||
           !identities.emplace(a.node,a.primitive).second || a.kind!=b.kind || a.ellipse!=b.ellipse ||
           a.style!=b.style || a.closed!=b.closed || a.filled!=b.filled || a.points.size()!=b.points.size())
            return std::nullopt;
        if((a.kind==DrawKind::Glyph || a.kind==DrawKind::Image) && !sameRect(a.a.rect,b.a.rect)) return std::nullopt;
        if(bool(a.image)!=bool(b.image)) return std::nullopt;
        if(a.image && (a.image->id!=b.image->id || a.image->width!=b.image->width || a.image->height!=b.image->height))
            return std::nullopt;
    }
    const double t=std::clamp(progress,0.,1.);
    if(t==0) return from;
    if(t==1) return to;
    const auto mix=[&](double a,double b) {return a+(b-a)*t;};
    const auto rect=[&](Rect a,const Rect& b) {return Rect{mix(a.x,b.x),mix(a.y,b.y),mix(a.w,b.w),mix(a.h,b.h)};};
    std::vector<DrawItem> result=from;
    for(std::size_t i=0;i<result.size();++i) {
        auto& item=result[i];const auto& a=from[i];const auto& b=to[i];
        item.quad=rect(a.quad,b.quad);item.clip=rect(a.clip,b.clip);
        if(a.kind!=DrawKind::Glyph && a.kind!=DrawKind::Image) item.a.rect=rect(a.a.rect,b.a.rect);
        item.b.rect=rect(a.b.rect,b.b.rect);
        for(std::size_t j=0;j<4;++j) {item.a.radii[j]=mix(a.a.radii[j],b.a.radii[j]);item.b.radii[j]=mix(a.b.radii[j],b.b.radii[j]);}
        for(std::size_t j=0;j<item.points.size();++j) item.points[j]={mix(a.points[j].x,b.points[j].x),mix(a.points[j].y,b.points[j].y)};
        item.sigma=mix(a.sigma,b.sigma);item.strokeWidth=mix(a.strokeWidth,b.strokeWidth);
        item.color={float(mix(a.color.r,b.color.r)),float(mix(a.color.g,b.color.g)),float(mix(a.color.b,b.color.b)),float(mix(a.color.a,b.color.a))};
    }
    return result;
}

std::optional<std::vector<DrawItem>> remapDrawListIdentities(
    const Document& from, const Id& fromRoot, const Document& to, const Id& toRoot,
    const std::vector<DrawItem>& destination) {
    // Components are expanded before matching: names address one occurrence,
    // never a shared source/master node. Structured paths cannot collide when
    // names contain separators or when branches reuse the same child name.
    using Path=std::vector<std::pair<std::string,NodeType>>;
    using Index=std::map<Path,Id>;
    const auto index=[](const Document& document,const Id& root)->std::optional<Index> {
        const auto* screen=document.view(root);
        if(!screen || screen->type!=NodeType::Frame || !screen->parent.isNil()) return std::nullopt;
        Index result;result.emplace(Path{},root);
        std::set<Id> visited{root};
        const auto collect=[&](auto&& self,const Id& parent,Path path)->bool {
            std::set<std::string> siblings;
            for(const auto& key:document.viewChildren(parent)) {
                const auto* node=document.view(key);
                if(!node || node->name.empty() || !siblings.emplace(node->name).second || !visited.emplace(key).second)
                    return false;
                auto child=path;child.emplace_back(node->name,node->type);
                if(!result.emplace(child,key).second || !self(self,key,std::move(child))) return false;
            }
            return true;
        };
        if(!collect(collect,root,{})) return std::nullopt;
        return result;
    };
    const auto source=index(from,fromRoot),target=index(to,toRoot);
    if(!source || !target || source->size()!=target->size()) return std::nullopt;
    std::map<Id,Id> identities;
    for(const auto& [path,key]:*target) {
        const auto corresponding=source->find(path);
        if(corresponding==source->end()) return std::nullopt;
        identities.emplace(key,corresponding->second);
    }
    auto result=destination;
    for(auto& paint:result) {
        const auto match=identities.find(paint.node);
        if(match==identities.end()) return std::nullopt;
        paint.node=match->second;
    }
    return result;
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
    Builder(const Document& document, const Rect& view, const Id& mode, std::vector<DrawItem>& out,const Id& root,Vec2 scroll)
        : document_(document), view_(view), mode_(mode), out_(out),root_(root),scroll_(scroll) {}

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
    std::uint32_t identify(std::size_t begin,const Id& id,std::uint32_t next=0) {
        for(std::size_t i=begin;i<out_.size();++i) {out_[i].node=id;out_[i].primitive=next++;}
        return next;
    }

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
            const auto ownStart=out_.size();
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
                identify(ownStart,id);continue;
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
                identify(ownStart,id);continue;
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
                identify(ownStart,id);continue;
            }
            if (node.type == NodeType::Text) {
                const auto text = layoutText(node.text, box.w, node.textWrap);
                const auto fill = document_.resolveColor(node.fill, mode_);
                Color color = fill ? withAlpha(*fill, alpha) : kUnresolvedColor;
                if (!fill) color.a = static_cast<float>(alpha);
                const auto textClip = intersect(clip, box);
                bool visible = false;
                std::uint32_t glyphIndex=0;
                if (node.fillVisible) for (const auto& glyph : text.glyphs) {
                    const auto slot=glyphIndex++;
                    const Rect quad = offset(glyph.box, box.x, box.y);
                    if (!quad.intersects(intersect(view_, textClip))) continue;
                    // Glyph items use a.rect for normalized atlas coordinates.
                    out_.push_back({DrawKind::Glyph, false, quad, {glyph.uv, {}}, {}, color, textClip});
                    out_.back().node=id;out_.back().primitive=slot;
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

            const auto nextPrimitive=identify(before,id);
            if (isContainer(node.type)) {
                const Rect childClip = node.clipContent ? intersect(clip, box) : clip;
                if (!node.clipContent || childClip.intersects(view_)) {
                    const Vec2 scroll=id==root_?scroll_:Vec2{};
                    collect(id, {box.x-scroll.x, box.y-scroll.y}, childClip, alpha);
                }
            }

            const auto strokeStart=out_.size();
            if (inView && stroke) {
                strokeItem(*stroke, box, radii, ellipse, clip, alpha);
            }
            identify(strokeStart,id,nextPrimitive);
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
    Vec2 scroll_;
};

} // namespace

std::size_t buildDrawList(const Document& document, const Rect& view, const Id& mode, std::vector<DrawItem>& out,const Id& root,Vec2 scroll) {
    return Builder{document, view, mode, out,root,scroll}.run();
}

} // namespace vc
