#pragma once

namespace vc::render {

// Each input is a complete premultiplied scene. Mixing before source-over keeps
// opaque screens opaque and applies opacity once to overlapping translucent paints.
inline constexpr const char* kPrototypeCompositeWgsl = R"(
struct Mix { amount: f32, }
@group(0) @binding(0) var<uniform> settings: Mix;
@group(0) @binding(1) var fromScene: texture_2d<f32>;
@group(0) @binding(2) var toScene: texture_2d<f32>;
@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
    let p=vec2f(f32((i << 1u) & 2u),f32(i & 2u));
    return vec4f(p*2.0-1.0,0.0,1.0);
}
@fragment fn fs(@builtin(position) position: vec4f) -> @location(0) vec4f {
    let pixel=vec2i(position.xy);
    return mix(textureLoad(fromScene,pixel,0),textureLoad(toScene,pixel,0),settings.amount);
}
)";

inline constexpr const char* kCommonWgsl = R"(
struct Frame {
    viewport: vec2f,
    zoom: f32,
    dpr: f32,
    gridOffset: vec2f,
    gridMinor: f32,
    gridMinorAlpha: f32,
    gridTint: f32,
    pad: f32,
}

@group(0) @binding(0) var<uniform> frame: Frame;

fn toClip(screen: vec2f) -> vec4f {
    let ndc = screen / frame.viewport * 2.0 - 1.0;
    return vec4f(ndc.x, -ndc.y, 0.0, 1.0);
}
)";

inline constexpr const char* kGridWgsl = R"(
@vertex
fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
    let p = vec2f(f32((i << 1u) & 2u), f32(i & 2u));
    return vec4f(p * 2.0 - 1.0, 0.0, 1.0);
}

fn lineCoverage(world: vec2f, spacing: f32) -> f32 {
    let toLine = abs(fract(world / spacing + 0.5) - 0.5) * spacing * frame.zoom * frame.dpr;
    let d = min(toLine.x, toLine.y);
    return clamp(1.0 - d, 0.0, 1.0);
}

@fragment
fn fs(@builtin(position) pos: vec4f) -> @location(0) vec4f {
    let world = pos.xy / frame.dpr / frame.zoom + frame.gridOffset;
    let minor = lineCoverage(world, frame.gridMinor) * 0.05 * frame.gridMinorAlpha;
    let major = lineCoverage(world, frame.gridMinor * 10.0) * 0.1;
    return vec4f(vec3f(frame.gridTint), max(minor, major));
}
)";

inline constexpr const char* kShapesWgsl = R"(
struct Item {
    quad: vec4f,
    color: vec4f,
    clip: vec4f,
    aRect: vec4f,
    aRadii: vec4f,
    bRect: vec4f,
    bRadii: vec4f,
    params: vec4f,
    extra: vec4f,
}

@group(0) @binding(1) var<storage, read> items: array<Item>;
@group(0) @binding(2) var glyphAtlas: texture_2d<f32>;
@group(0) @binding(3) var glyphSampler: sampler;
@group(0) @binding(4) var<storage,read> pathVertices: array<vec4f>;
@group(0) @binding(5) var imageAtlas: texture_2d_array<f32>;

const kFill = 0u;
const kStroke = 1u;
const kDropShadow = 2u;
const kInnerShadow = 3u;
const kOutline = 4u;
const kOutlineWidthPx = 1.5;
const kGlyphAtlasSize = 4096.0;
const kGlyphAtlasMaxLod = 3.0;

struct VertexOut {
    @builtin(position) pos: vec4f,
    @location(0) world: vec2f,
    @location(1) @interpolate(flat) index: u32,
}

@vertex
fn vs(@builtin(vertex_index) v: u32, @builtin(instance_index) i: u32) -> VertexOut {
    let item = items[i];
    let corner = vec2f(f32(v & 1u), f32(v >> 1u));
    let aaPad = 2.0 / frame.zoom;
    var out: VertexOut;
    out.world = item.quad.xy - aaPad + corner * (item.quad.zw + 2.0 * aaPad);
    out.pos = toClip(out.world * frame.zoom);
    out.index = i;
    return out;
}

fn boxDistance(p: vec2f, rect: vec4f, radii: vec4f) -> f32 {
    let q0 = p - rect.xy;
    let top = select(radii.x, radii.y, q0.x > 0.0);
    let bottom = select(radii.w, radii.z, q0.x > 0.0);
    let r = min(select(top, bottom, q0.y > 0.0), min(rect.z, rect.w));
    let q = abs(q0) - rect.zw + r;
    return length(max(q, vec2f(0.0))) + min(max(q.x, q.y), 0.0) - r;
}

fn ellipseDistance(p: vec2f, rect: vec4f) -> f32 {
    let half = max(rect.zw, vec2f(1e-4));
    return (length((p - rect.xy) / half) - 1.0) * min(half.x, half.y);
}

fn shapeDistance(p: vec2f, rect: vec4f, radii: vec4f, ellipse: bool) -> f32 {
    if (ellipse) {
        return ellipseDistance(p, rect);
    }
    return boxDistance(p, rect, radii);
}

fn coverage(d: f32) -> f32 {
    return clamp(0.5 - d * frame.zoom * frame.dpr, 0.0, 1.0);
}

fn erf(x: f32) -> f32 {
    let s = sign(x);
    let a = abs(x);
    var r = 1.0 + (0.278393 + (0.230389 + 0.078108 * (a * a)) * a) * a;
    r = r * r;
    return s - s / (r * r);
}

fn blurred(d: f32, sigma: f32) -> f32 {
    if (sigma * frame.zoom * frame.dpr < 0.5) {
        return coverage(d);
    }
    return 0.5 - 0.5 * erf(d / (sigma * 1.41421356));
}

/// 1 on the drawn parts of a dashed, dotted or double stroke band, 0 in the gaps.
fn strokePattern(p: vec2f, item: Item, dOuter: f32) -> f32 {
    let style = u32(item.extra.x);
    let width = max(item.params.w, 1e-3);
    if (style == 4u) {
        let depth = -dOuter / width;
        return select(0.0, 1.0, depth < 0.34 || depth > 0.66);
    }
    if (style != 2u && style != 3u) {
        return 1.0;
    }
    let q = p - item.aRect.xy;
    let half = max(item.aRect.zw, vec2f(1e-4));
    var t = q.x + half.x;
    if (item.params.y > 0.5) {
        t = (atan2(q.y / half.y, q.x / half.x) + 3.14159265) * (half.x + half.y) * 0.5;
    } else if (abs(q.x) / half.x > abs(q.y) / half.y) {
        t = q.y + half.y;
    }
    let dash = select(3.0 * width, width, style == 3u);
    let gap = select(2.0 * width, width, style == 3u);
    return select(0.0, 1.0, fract(t / (dash + gap)) * (dash + gap) < dash);
}

@fragment
fn fs(in: VertexOut) -> @location(0) vec4f {
    let item = items[in.index];
    let p = in.world;
    if (any(p < item.clip.xy) || any(p > item.clip.zw)) {
        discard;
    }
    let kind = u32(item.params.x);
    let ellipse = item.params.y > 0.5;
    let sigma = item.params.z;
    if(kind==8u) {
        let local=(p-item.quad.xy)/item.quad.zw;
        if(any(local<vec2f(0.0)) || any(local>vec2f(1.0))) {discard;}
        let uv=(item.aRect.xy+local*item.aRect.zw)*item.extra.xy;
        let sampled=textureSampleLevel(imageAtlas,glyphSampler,clamp(uv,vec2f(0.5/512.0),item.extra.xy-vec2f(0.5/512.0)),i32(item.extra.w),0.0);
        // Uploads are premultiplied so filtering weights colors by their actual coverage.
        // The shared shape blend uses SrcAlpha, so recover straight RGB after filtering.
        let rgb=sampled.rgb/max(sampled.a,1e-6);
        return vec4f(rgb,sampled.a*item.color.a);
    }
    if (kind == 7u) {
        let count=u32(item.extra.y);
        let flags=u32(item.extra.z);
        let offset=u32(item.extra.w);
        if(count<2u) {discard;}
        var distance=1e30;
        var inside=false;
        let segments=count-1u+(flags&1u);
        for(var i=0u;i<segments;i++) {
            let a=pathVertices[offset+i].xy;
            let b=pathVertices[offset+(i+1u)%count].xy;
            let direction=b-a;
            let t=clamp(dot(p-a,direction)/max(dot(direction,direction),1e-12),0.0,1.0);
            distance=min(distance,length(p-a-t*direction));
            if((a.y>p.y)!=(b.y>p.y)) {
                if(p.x<a.x+(p.y-a.y)*(b.x-a.x)/(b.y-a.y)) {inside=!inside;}
            }
        }
        var edge=1e30;
        if(item.params.w>0.0) {edge=distance-item.params.w*0.5;}
        if((flags&2u)!=0u) {edge=min(edge,select(distance,-distance,inside));}
        return vec4f(item.color.rgb,item.color.a*coverage(edge));
    }
    if (kind == 6u) {
        let direction = item.bRect.xy - item.aRect.xy;
        let t = clamp(dot(p-item.aRect.xy,direction)/max(dot(direction,direction),1e-12),0.0,1.0);
        let segmentDistance = length(p-item.aRect.xy-t*direction)-item.params.w*0.5;
        return vec4f(item.color.rgb,item.color.a*coverage(segmentDistance));
    }
    if (kind == 5u) {
        let local = (p - item.quad.xy) / item.quad.zw;
        if (any(local < vec2f(0.0)) || any(local > vec2f(1.0))) { discard; }
        // Box-filter one device pixel with a 4x4 grid of taps from a finer mip level.
        let texelsPerPixel = item.aRect.zw / item.quad.zw * kGlyphAtlasSize / (frame.zoom * frame.dpr);
        let lod = clamp(log2(max(texelsPerPixel.x, texelsPerPixel.y)) - 1.5, 0.0, kGlyphAtlasMaxLod);
        let uv = item.aRect.xy + local * item.aRect.zw;
        let step = texelsPerPixel / kGlyphAtlasSize / 4.0;
        var mask = 0.0;
        for (var j = 0; j < 4; j++) {
            for (var i = 0; i < 4; i++) {
                let offset = (vec2f(f32(i), f32(j)) - 1.5) * step;
                mask += textureSampleLevel(glyphAtlas, glyphSampler, uv + offset, lod).r;
            }
        }
        return vec4f(item.color.rgb, item.color.a * mask / 16.0);
    }
    let dA = shapeDistance(p, item.aRect, item.aRadii, ellipse);

    var alpha = 0.0;
    if (kind == kFill) {
        alpha = coverage(dA);
    } else if (kind == kStroke) {
        let dB = shapeDistance(p, item.bRect, item.bRadii, ellipse);
        alpha = coverage(dA) * (1.0 - coverage(dB)) * strokePattern(p, item, dA);
    } else if (kind == kDropShadow) {
        alpha = blurred(dA, sigma);
    } else if (kind == kInnerShadow) {
        let dB = shapeDistance(p, item.bRect, item.bRadii, ellipse);
        alpha = coverage(dB) * (1.0 - blurred(dA, sigma));
    } else if (kind == kOutline) {
        let ring = abs(boxDistance(p, item.aRect, vec4f(0.0))) - kOutlineWidthPx * 0.5 / frame.zoom;
        alpha = coverage(ring);
    }
    return vec4f(item.color.rgb, item.color.a * alpha);
}
)";

} // namespace vc::render
