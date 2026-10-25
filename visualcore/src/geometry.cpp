#include "vc/geometry.hpp"
#include <algorithm>
#include <cmath>

namespace vc {
namespace {

void hitChildren(const Document& document, const Id& parent, Vec2 offset, Vec2 world, bool framesOnly,
                 const Id& mode, Id& hit, double tolerance, const Id& root, bool includeLocked) {
    for (const Id& id : document.viewChildren(parent)) {
        if(parent.isNil() && !root.isNil() && id!=root) continue;
        const Node& node = *document.view(id);
        if (!node.visible) {
            continue;
        }
        const Rect box = document.box(id, mode);
        const Rect bounds{offset.x + box.x, offset.y + box.y, box.w, box.h};
        bool inside = bounds.contains(world);
        if (node.type == NodeType::Line) {
            const auto ends=lineEndpoints(node,bounds); const auto d=ends[1]-ends[0]; const auto p=world-ends[0];
            const double length=d.x*d.x+d.y*d.y;
            const double t=length>0 ? std::clamp((p.x*d.x+p.y*d.y)/length,0.0,1.0) : 0;
            inside=node.fillVisible && node.lineWidth>0 && std::hypot(p.x-t*d.x,p.y-t*d.y)<=node.lineWidth/2+tolerance;
        }
        if (node.type == NodeType::Path) {
            inside=false;
            const auto points=flattenPath(node,bounds);
            bool interior=false;
            if(node.fillVisible) for(std::size_t i=1;i<points.size()+(node.path.closed?1u:0u);++i) {
                const auto a=points[i-1], b=points[i%points.size()], d=b-a, p=world-a;
                const double length=d.x*d.x+d.y*d.y;
                const double t=length>0?std::clamp((p.x*d.x+p.y*d.y)/length,0.0,1.0):0;
                if((node.lineWidth>0 || node.path.filled) && std::hypot(p.x-t*d.x,p.y-t*d.y)<=node.lineWidth/2+tolerance) inside=true;
                if((a.y>world.y)!=(b.y>world.y) && world.x<a.x+(world.y-a.y)*(b.x-a.x)/(b.y-a.y)) interior=!interior;
            }
            inside=inside || (node.fillVisible && node.path.filled && interior);
        }
        if (inside && (includeLocked || !node.locked) && (!framesOnly || node.type == NodeType::Frame || node.type == NodeType::Component)) {
            hit = id;
        }
        if (isContainer(node.type) && (inside || !node.clipContent)) {
            hitChildren(document, id, {bounds.x, bounds.y}, world, framesOnly, mode, hit, tolerance,root,includeLocked);
        }
    }
}

void frameChildren(const Document& document, const Id& parent, Vec2 offset, Vec2 world, const Id& mode,
                   const std::unordered_set<Id>& excluded, Id& hit) {
    for (const Id& id : document.viewChildren(parent)) {
        const Node& node = *document.view(id);
        const bool frame = node.type == NodeType::Frame || node.type == NodeType::Component;
        if (!node.visible || excluded.contains(id) || !frame || document.ownerOf(id) != id) {
            continue;
        }
        const Rect box = document.box(id, mode);
        const Rect bounds{offset.x + box.x, offset.y + box.y, box.w, box.h};
        if (bounds.contains(world)) {
            if (!node.locked) hit = id;
            frameChildren(document, id, {bounds.x, bounds.y}, world, mode, excluded, hit);
        }
    }
}

} // namespace

std::array<Vec2,2> lineEndpoints(const Node& node,const Rect& box) {
    return {Vec2{box.x,box.y+(node.lineReverse?box.h:0)},Vec2{box.x+box.w,box.y+(node.lineReverse?0:box.h)}};
}
NodePatch lineGeometry(Vec2 a,Vec2 b) {
    NodePatch patch; patch.x=std::min(a.x,b.x); patch.y=std::min(a.y,b.y);
    patch.w=std::abs(b.x-a.x); patch.h=std::abs(b.y-a.y); patch.lineReverse=(b.x-a.x)*(b.y-a.y)<0;
    return patch;
}

std::vector<Vec2> pathPoints(const Node& node,const Rect& box) {
    std::vector<Vec2> out; out.reserve(node.path.points.size());
    for(const auto p:node.path.points) out.push_back({box.x+p.x*box.w,box.y+p.y*box.h});
    return out;
}
Path pathWorldGeometry(const Node& node,const Rect& box) {
    auto path=node.path;
    for(auto* list:{&path.points,&path.inHandles,&path.outHandles}) for(auto& p:*list)
        p={box.x+p.x*box.w,box.y+p.y*box.h};
    return path;
}
NodePatch pathGeometry(const std::vector<Vec2>& points,bool closed) {
    return pathGeometry(Path{points,closed});
}
NodePatch pathGeometry(const Path& world) {
    NodePatch patch;patch.path=Path{};
    if(world.points.size()<(world.closed?3u:2u) || world.points.size()>64 || (world.filled && !world.closed)) return patch;
    if((!world.inHandles.empty() || !world.outHandles.empty()) &&
       (world.inHandles.size()!=world.points.size() || world.outHandles.size()!=world.points.size())) return patch;
    double x=world.points[0].x,y=world.points[0].y,right=x,bottom=y;
    for(const auto* list:{&world.points,&world.inHandles,&world.outHandles}) for(const auto p:*list) {
        if(!std::isfinite(p.x)||!std::isfinite(p.y)) return patch;
        x=std::min(x,p.x);y=std::min(y,p.y);right=std::max(right,p.x);bottom=std::max(bottom,p.y);
    }
    if(!std::isfinite(right-x)||!std::isfinite(bottom-y)) return patch;
    patch.x=x;patch.y=y;patch.w=right-x;patch.h=bottom-y;
    auto path=world;
    for(auto* list:{&path.points,&path.inHandles,&path.outHandles}) for(auto& p:*list)
        p={right>x?(p.x-x)/(right-x):0,bottom>y?(p.y-y)/(bottom-y):0};
    patch.path=std::move(path);
    return patch;
}
std::vector<Vec2> flattenPath(const Node& node,const Rect& box,double tolerance) {
    const auto path=pathWorldGeometry(node,box);
    if(path.inHandles.empty()) return path.points;
    if(path.points.size()<2 || path.inHandles.size()!=path.points.size() || path.outHandles.size()!=path.points.size()) return {};
    tolerance=std::isfinite(tolerance)?std::max(tolerance,1e-6):.25;
    const auto midpoint=[](Vec2 a,Vec2 b){return Vec2{a.x+(b.x-a.x)*.5,a.y+(b.y-a.y)*.5};};
    const auto distance=[](Vec2 p,Vec2 a,Vec2 b){
        const auto d=b-a,q=p-a;const double length=d.x*d.x+d.y*d.y;
        const auto t=length>0?std::clamp((q.x*d.x+q.y*d.y)/length,0.,1.):0.;
        return std::hypot(q.x-t*d.x,q.y-t*d.y);
    };
    std::vector<Vec2> out;out.reserve(256);out.push_back(path.points[0]);
    const auto flatten=[&](Vec2 a,Vec2 b,Vec2 c,Vec2 d,int depth,auto&& self)->void {
        // Six binary subdivisions per authored segment bound the whole path to 4096 points.
        if(depth>=6 || std::max(distance(b,a,d),distance(c,a,d))<=tolerance) {out.push_back(d);return;}
        const auto ab=midpoint(a,b),bc=midpoint(b,c),cd=midpoint(c,d);
        const auto abc=midpoint(ab,bc),bcd=midpoint(bc,cd),mid=midpoint(abc,bcd);
        self(a,ab,abc,mid,depth+1,self);self(mid,bcd,cd,d,depth+1,self);
    };
    const auto count=path.points.size();
    for(std::size_t i=0;i<count-(path.closed?0u:1u);++i) {
        const auto j=(i+1)%count;
        flatten(path.points[i],path.outHandles[i],path.inHandles[j],path.points[j],0,flatten);
    }
    if(path.closed) out.pop_back();
    return out;
}

Id frameAt(const Document& document, Vec2 world, const Id& mode, const std::unordered_set<Id>& excluded) {
    Id hit;
    frameChildren(document, Id{}, {}, world, mode, excluded, hit);
    return hit;
}

std::optional<Rect> worldBounds(const Document& document, const Id& id, const Id& mode) {
    if (!document.view(id)) {
        return std::nullopt;
    }
    Rect bounds = document.box(id, mode);
    for (const Node* p = document.view(document.view(id)->parent); p; p = document.view(p->parent)) {
        const Rect parent = document.box(p->id, mode);
        bounds.x += parent.x;
        bounds.y += parent.y;
    }
    return bounds;
}

Id hitTest(const Document& document, Vec2 world, bool framesOnly, const Id& mode,double tolerance,const Id& root,bool includeLocked) {
    Id hit;
    if(!root.isNil()) {
        const auto* node=document.find(root);
        const auto box=worldBounds(document,root,mode);
        if(!node || !node->parent.isNil() || !node->visible || !box || !box->contains(world)) return {};
    }
    hitChildren(document, Id{}, {}, world, framesOnly, mode, hit, tolerance,root,includeLocked);
    return hit;
}

} // namespace vc
