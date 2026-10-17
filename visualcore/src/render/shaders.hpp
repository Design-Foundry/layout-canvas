#pragma once

namespace vc::render {

inline constexpr const char* kCommonWgsl = R"(
struct Frame {
    viewport: vec2f,
    zoom: f32,
    dpr: f32,
    gridOffset: vec2f,
    gridMinor: f32,
    gridMinorAlpha: f32,
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
    return vec4f(1.0, 1.0, 1.0, max(minor, major));
}
)";

inline constexpr const char* kShapesWgsl = R"(
struct Shape {
    pos: vec2f,
    size: vec2f,
    color: vec4f,
    clip: vec4f,
    radius: f32,
    kind: u32,
}

@group(0) @binding(1) var<storage, read> shapes: array<Shape>;

struct VertexOut {
    @builtin(position) pos: vec4f,
    @location(0) color: vec4f,
    @location(1) local: vec2f,
    @location(2) world: vec2f,
    @location(3) @interpolate(flat) clip: vec4f,
    @location(4) @interpolate(flat) shape: vec4f,
}

@vertex
fn vs(@builtin(vertex_index) v: u32, @builtin(instance_index) i: u32) -> VertexOut {
    let s = shapes[i];
    let corner = vec2f(f32(v & 1u), f32(v >> 1u));
    let aaPad = 2.0 / frame.zoom;
    let local = corner * (s.size + 2.0 * aaPad) - aaPad;

    var out: VertexOut;
    out.world = s.pos + local;
    out.pos = toClip(out.world * frame.zoom);
    out.color = s.color;
    out.local = local;
    out.clip = s.clip;
    out.shape = vec4f(s.size, s.radius, f32(s.kind));
    return out;
}

const kOutlineWidthPx = 1.5;

fn boxDistance(p: vec2f, half: vec2f, radius: f32) -> f32 {
    let r = min(radius, min(half.x, half.y));
    let q = abs(p) - half + r;
    return length(max(q, vec2f(0.0))) + min(max(q.x, q.y), 0.0) - r;
}

fn ellipseDistance(p: vec2f, half: vec2f) -> f32 {
    return (length(p / half) - 1.0) * min(half.x, half.y);
}

@fragment
fn fs(in: VertexOut) -> @location(0) vec4f {
    if (any(in.world < in.clip.xy) || any(in.world > in.clip.zw)) {
        discard;
    }
    let half = in.shape.xy * 0.5;
    let p = in.local - half;
    var d: f32;
    if (in.shape.w > 1.5) {
        d = abs(boxDistance(p, half, 0.0)) - kOutlineWidthPx * 0.5 / frame.zoom;
    } else if (in.shape.w > 0.5) {
        d = ellipseDistance(p, half);
    } else {
        d = boxDistance(p, half, in.shape.z);
    }
    let coverage = clamp(0.5 - d * frame.zoom * frame.dpr, 0.0, 1.0);
    return vec4f(in.color.rgb, in.color.a * coverage);
}
)";

} // namespace vc::render
