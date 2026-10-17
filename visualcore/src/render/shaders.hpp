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

inline constexpr const char* kRectsWgsl = R"(
struct Rect {
    pos: vec2f,
    size: vec2f,
    color: vec4f,
}

@group(0) @binding(1) var<storage, read> rects: array<Rect>;

struct VertexOut {
    @builtin(position) pos: vec4f,
    @location(0) color: vec4f,
}

@vertex
fn vs(@builtin(vertex_index) v: u32, @builtin(instance_index) i: u32) -> VertexOut {
    let r = rects[i];
    let corner = vec2f(f32(v & 1u), f32(v >> 1u));
    var out: VertexOut;
    out.pos = toClip((r.pos + corner * r.size) * frame.zoom);
    out.color = r.color;
    return out;
}

@fragment
fn fs(in: VertexOut) -> @location(0) vec4f {
    return in.color;
}
)";

} // namespace vc::render
