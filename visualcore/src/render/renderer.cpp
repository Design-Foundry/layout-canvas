#include "renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/val.h>
#include <webgpu/webgpu_cpp.h>

#include "shaders.hpp"
#include "vc/color.hpp"
#include "vc/draw_list.hpp"
#include "vc/geometry.hpp"

namespace vc::render {
namespace {

struct FrameUniform {
    float viewport[2];
    float zoom;
    float dpr;
    float gridOffset[2];
    float gridMinor;
    float gridMinorAlpha;
};

/// One draw item as the shaders read it. Rects are center and half size, relative to the camera
/// origin; radii are top-start, top-end, bottom-end, bottom-start.
struct GpuItem {
    float quad[4];
    float color[4];
    float clip[4];
    float aRect[4];
    float aRadii[4];
    float bRect[4];
    float bRadii[4];
    /// kind, ellipse flag, sigma, stroke width.
    float params[4];
    /// stroke style, unused.
    float extra[4];
};
static_assert(sizeof(GpuItem) == 144, "GpuItem must match the WGSL Item storage stride");

constexpr double kUnbounded = 1e30;
constexpr Oklch kSelectionColor{0.62, 0.19, 255.0};
constexpr Oklch kComponentColor{0.58, 0.22, 300.0};
constexpr Rect kNoClip{-kUnbounded, -kUnbounded, 2 * kUnbounded, 2 * kUnbounded};

constexpr double kMinGridSpacingPx = 8.0;
constexpr double kGridFadeEndPx = 32.0;
constexpr std::size_t kInitialShapeCapacity = 1024;
constexpr double kStatsSmoothing = 0.1;

double positiveMod(double value, double modulus) {
    const double r = std::fmod(value, modulus);
    return r < 0.0 ? r + modulus : r;
}

Rect intersect(const Rect& a, const Rect& b) {
    const double x0 = std::max(a.x, b.x);
    const double y0 = std::max(a.y, b.y);
    const double x1 = std::min(a.x + a.w, b.x + b.w);
    const double y1 = std::min(a.y + a.h, b.y + b.h);
    return {x0, y0, std::max(0.0, x1 - x0), std::max(0.0, y1 - y0)};
}

wgpu::ShaderModule createShader(const wgpu::Device& device, const std::string& code) {
    wgpu::ShaderSourceWGSL wgsl{};
    wgsl.code = code.c_str();
    wgpu::ShaderModuleDescriptor desc{};
    desc.nextInChain = &wgsl;
    return device.CreateShaderModule(&desc);
}

} // namespace

struct Renderer::State {
    std::string selector;
    const Camera& camera;
    const Document& document;
    const Editor& editor;
    std::string cursor;

    wgpu::Instance instance;
    wgpu::Surface surface;
    wgpu::Adapter adapter;
    wgpu::Device device;
    wgpu::TextureFormat format = wgpu::TextureFormat::Undefined;
    uint32_t width = 0;
    uint32_t height = 0;
    double cssWidth = 0.0;
    double cssHeight = 0.0;
    double dpr = 1.0;

    wgpu::BindGroupLayout bindGroupLayout;
    wgpu::RenderPipeline gridPipeline;
    wgpu::RenderPipeline shapesPipeline;
    wgpu::Buffer frameBuffer;
    wgpu::Buffer shapesBuffer;
    wgpu::Buffer pointsBuffer;
    std::size_t pointsCapacity = 0;
    std::size_t shapesCapacity = 0;
    wgpu::BindGroup bindGroup;
    wgpu::Texture glyphTexture;
    wgpu::TextureView glyphView;
    wgpu::Texture imageTexture;
    wgpu::TextureView imageView;
    std::vector<std::string> cachedImages;
    wgpu::Sampler glyphSampler;

    std::vector<DrawItem> drawItems;
    std::vector<GpuItem> instances;
    std::vector<std::array<float,4>> pathVertices;
    Rect view;
    Vec2 origin;

    bool running = false;
    bool looping = false;
    double lastTime = 0.0;
    FrameStats stats;

    State(std::string selector, const Camera& camera, const Document& document, const Editor& editor)
        : selector(std::move(selector)), camera(camera), document(document), editor(editor) {}

    void requestAdapter();
    void requestDevice();
    void onDevice();
    void createPipelines();
    void ensureShapesCapacity(std::size_t count,std::size_t pointCount = 1);
    void startLoop();
    bool resizeIfNeeded();
    void writeFrameUniform();
    void buildInstances();
    void uploadImages();
    void addSelectionOutlines();
    void applyCursor();
    bool frame(double time);
};

Renderer::Renderer(std::string canvasSelector, const Camera& camera, const Document& document, const Editor& editor)
    : state_(std::make_unique<State>(std::move(canvasSelector), camera, document, editor)) {}

Renderer::~Renderer() = default;

void Renderer::start() {
    state_->running = true;
    if (state_->device) {
        state_->startLoop();
        return;
    }
    if (state_->instance) {
        return;
    }

    state_->instance = wgpu::CreateInstance(nullptr);

    wgpu::EmscriptenSurfaceSourceCanvasHTMLSelector source{};
    source.selector = state_->selector.c_str();

    wgpu::SurfaceDescriptor surfaceDesc{};
    surfaceDesc.nextInChain = &source;

    state_->surface = state_->instance.CreateSurface(&surfaceDesc);

    state_->requestAdapter();
}

void Renderer::stop() {
    state_->running = false;
}

FrameStats Renderer::stats() const {
    return state_->stats;
}

void Renderer::State::requestAdapter() {
    wgpu::RequestAdapterOptions options{};

    instance.RequestAdapter(
        &options, wgpu::CallbackMode::AllowSpontaneous,
        [this](wgpu::RequestAdapterStatus status, wgpu::Adapter result, wgpu::StringView message) {
            if (status != wgpu::RequestAdapterStatus::Success) {
                std::printf("adapter request failed: %.*s\n", int(message.length), message.data);
                return;
            }
            adapter = std::move(result);
            requestDevice();
        });
}

void Renderer::State::requestDevice() {
    wgpu::DeviceDescriptor desc{};
    desc.SetUncapturedErrorCallback(
        [](const wgpu::Device&, wgpu::ErrorType type, wgpu::StringView message) {
            std::printf("webgpu error %d: %.*s\n", int(type), int(message.length), message.data);
        });

    adapter.RequestDevice(
        &desc, wgpu::CallbackMode::AllowSpontaneous,
        [this](wgpu::RequestDeviceStatus status, wgpu::Device result, wgpu::StringView message) {
            if (status != wgpu::RequestDeviceStatus::Success) {
                std::printf("device request failed: %.*s\n", int(message.length), message.data);
                return;
            }
            device = std::move(result);
            onDevice();
        });
}

void Renderer::State::onDevice() {
    wgpu::SurfaceCapabilities caps;
    surface.GetCapabilities(adapter, &caps);
    format = caps.formats[0];

    createPipelines();
    ensureShapesCapacity(kInitialShapeCapacity);

    if (running) {
        startLoop();
    }
}

void Renderer::State::createPipelines() {
    wgpu::TextureDescriptor textureDesc{};
    textureDesc.size = {kTextAtlasSize, kTextAtlasSize, 1};
    textureDesc.format = wgpu::TextureFormat::R8Unorm;
    textureDesc.mipLevelCount = kTextAtlasMipLevels;
    textureDesc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
    glyphTexture = device.CreateTexture(&textureDesc);
    glyphView = glyphTexture.CreateView();
    wgpu::SamplerDescriptor samplerDesc{};
    samplerDesc.magFilter = wgpu::FilterMode::Linear;
    samplerDesc.minFilter = wgpu::FilterMode::Linear;
    samplerDesc.mipmapFilter = wgpu::MipmapFilterMode::Linear;
    glyphSampler = device.CreateSampler(&samplerDesc);
    const auto atlas = textAtlas();
    std::vector<std::uint8_t> level(atlas.begin(), atlas.end());
    for (uint32_t mip = 0, size = kTextAtlasSize; mip < kTextAtlasMipLevels; ++mip, size /= 2) {
        if (mip > 0) {
            for (uint32_t y = 0; y < size; ++y)
                for (uint32_t x = 0; x < size; ++x) {
                    const auto at = [&](uint32_t dx, uint32_t dy) { return unsigned(level[(2 * y + dy) * 2 * size + 2 * x + dx]); };
                    level[y * size + x] = std::uint8_t((at(0, 0) + at(1, 0) + at(0, 1) + at(1, 1) + 2) / 4);
                }
        }
        wgpu::TexelCopyTextureInfo destination{}; destination.texture = glyphTexture; destination.mipLevel = mip;
        wgpu::TexelCopyBufferLayout dataLayout{}; dataLayout.bytesPerRow = size; dataLayout.rowsPerImage = size;
        const wgpu::Extent3D extent{size, size, 1};
        device.GetQueue().WriteTexture(&destination, level.data(), std::size_t(size) * size, &dataLayout, &extent);
    }

    wgpu::TextureDescriptor imageDesc{};imageDesc.size={512,512,8};imageDesc.format=wgpu::TextureFormat::RGBA8Unorm;
    imageDesc.usage=wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
    imageTexture=device.CreateTexture(&imageDesc);
    wgpu::TextureViewDescriptor imageViewDesc{};imageViewDesc.dimension=wgpu::TextureViewDimension::e2DArray;
    imageView=imageTexture.CreateView(&imageViewDesc);
    wgpu::BindGroupLayoutEntry entries[6]{};
    entries[0].binding = 0;
    entries[0].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
    entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
    entries[1].binding = 1;
    entries[1].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
    entries[1].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
    entries[2].binding = 2;
    entries[2].visibility = wgpu::ShaderStage::Fragment;
    entries[2].texture.sampleType = wgpu::TextureSampleType::Float;
    entries[2].texture.viewDimension = wgpu::TextureViewDimension::e2D;
    entries[3].binding = 3;
    entries[3].visibility = wgpu::ShaderStage::Fragment;
    entries[3].sampler.type = wgpu::SamplerBindingType::Filtering;
    entries[4].binding = 4;
    entries[4].visibility = wgpu::ShaderStage::Fragment;
    entries[4].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
    entries[5].binding=5;entries[5].visibility=wgpu::ShaderStage::Fragment;
    entries[5].texture.sampleType=wgpu::TextureSampleType::Float;entries[5].texture.viewDimension=wgpu::TextureViewDimension::e2DArray;

    wgpu::BindGroupLayoutDescriptor layoutDesc{};
    layoutDesc.entryCount = 6;
    layoutDesc.entries = entries;
    bindGroupLayout = device.CreateBindGroupLayout(&layoutDesc);

    wgpu::PipelineLayoutDescriptor pipelineLayoutDesc{};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &bindGroupLayout;
    wgpu::PipelineLayout pipelineLayout = device.CreatePipelineLayout(&pipelineLayoutDesc);

    wgpu::BlendState blend{};
    blend.color = {wgpu::BlendOperation::Add, wgpu::BlendFactor::SrcAlpha,
                   wgpu::BlendFactor::OneMinusSrcAlpha};
    blend.alpha = {wgpu::BlendOperation::Add, wgpu::BlendFactor::One,
                   wgpu::BlendFactor::OneMinusSrcAlpha};

    wgpu::ColorTargetState target{};
    target.format = format;
    target.blend = &blend;

    auto createPipeline = [&](const char* body, wgpu::PrimitiveTopology topology) {
        wgpu::ShaderModule module = createShader(device, std::string{kCommonWgsl} + body);

        wgpu::FragmentState fragment{};
        fragment.module = module;
        fragment.entryPoint = "fs";
        fragment.targetCount = 1;
        fragment.targets = &target;

        wgpu::RenderPipelineDescriptor desc{};
        desc.layout = pipelineLayout;
        desc.vertex.module = module;
        desc.vertex.entryPoint = "vs";
        desc.primitive.topology = topology;
        desc.fragment = &fragment;
        return device.CreateRenderPipeline(&desc);
    };

    gridPipeline = createPipeline(kGridWgsl, wgpu::PrimitiveTopology::TriangleList);
    shapesPipeline = createPipeline(kShapesWgsl, wgpu::PrimitiveTopology::TriangleStrip);

    wgpu::BufferDescriptor frameDesc{};
    frameDesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    frameDesc.size = sizeof(FrameUniform);
    frameBuffer = device.CreateBuffer(&frameDesc);
}

void Renderer::State::ensureShapesCapacity(std::size_t count,std::size_t pointCount) {
    count=std::max(count,std::size_t{1});pointCount=std::max(pointCount,std::size_t{1});
    if(count<=shapesCapacity && pointCount<=pointsCapacity) return;
    wgpu::BufferDescriptor desc{};desc.usage=wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
    if(count>shapesCapacity) {
        shapesCapacity=std::max(count,shapesCapacity*2);
        desc.size=shapesCapacity*sizeof(GpuItem);shapesBuffer=device.CreateBuffer(&desc);
    }
    if(pointCount>pointsCapacity) {
        pointsCapacity=std::max(pointCount,pointsCapacity*2);
        desc.size=pointsCapacity*sizeof(std::array<float,4>);pointsBuffer=device.CreateBuffer(&desc);
    }
    wgpu::BindGroupEntry entries[6]{};
    entries[0].binding=0;entries[0].buffer=frameBuffer;
    entries[1].binding=1;entries[1].buffer=shapesBuffer;
    entries[2].binding=2;entries[2].textureView=glyphView;
    entries[3].binding=3;entries[3].sampler=glyphSampler;
    entries[4].binding=4;entries[4].buffer=pointsBuffer;
    entries[5].binding=5;entries[5].textureView=imageView;
    wgpu::BindGroupDescriptor groupDesc{};groupDesc.layout=bindGroupLayout;groupDesc.entryCount=6;groupDesc.entries=entries;
    bindGroup=device.CreateBindGroup(&groupDesc);
}

void Renderer::State::startLoop() {
    if (looping) {
        return;
    }
    looping = true;
    lastTime = 0.0;
    emscripten_request_animation_frame_loop(
        [](double time, void* user) { return static_cast<State*>(user)->frame(time); }, this);
}

bool Renderer::State::resizeIfNeeded() {
    emscripten_get_element_css_size(selector.c_str(), &cssWidth, &cssHeight);
    dpr = emscripten_get_device_pixel_ratio();

    const auto newWidth = static_cast<uint32_t>(std::lround(cssWidth * dpr));
    const auto newHeight = static_cast<uint32_t>(std::lround(cssHeight * dpr));
    if (newWidth == 0 || newHeight == 0) {
        return false;
    }
    if (newWidth == width && newHeight == height) {
        return true;
    }

    width = newWidth;
    height = newHeight;
    emscripten_set_canvas_element_size(selector.c_str(), int(width), int(height));

    wgpu::SurfaceConfiguration config{};
    config.device = device;
    config.format = format;
    config.width = width;
    config.height = height;
    surface.Configure(&config);
    return true;
}

void Renderer::State::writeFrameUniform() {
    const double zoom = camera.zoom();
    const double minor = std::max(1.0, std::pow(10.0, std::ceil(std::log10(kMinGridSpacingPx / zoom))));
    const double major = minor * 10.0;
    const double minorPx = minor * zoom;
    const double fade =
        std::clamp((minorPx - kMinGridSpacingPx) / (kGridFadeEndPx - kMinGridSpacingPx), 0.0, 1.0);

    const FrameUniform uniform{
        {float(cssWidth), float(cssHeight)},
        float(zoom),
        float(dpr),
        {float(positiveMod(camera.position().x, major)),
         float(positiveMod(camera.position().y, major))},
        float(minor),
        float(fade),
    };
    device.GetQueue().WriteBuffer(frameBuffer, 0, &uniform, sizeof(uniform));
}

void Renderer::State::uploadImages() {
    // Content hashes make immutable pixel assets share one layer across instances and crops.
    std::map<std::string,const Raster*> used;
    for(const auto& item:drawItems) if(item.image) used.emplace(item.image->id,item.image);
    std::vector<std::string> ids;for(const auto& [id,image]:used) ids.push_back(id);
    if(ids==cachedImages) return;
    std::size_t layer=0;
    for(const auto& [id,image]:used) {
        if(layer>=8) break; // Document validation enforces the same limit before rendering.
        const auto pixels=decodeRaster(*image);
        std::vector<std::uint8_t> padded(512*512*4,0);
        if(pixels) {
            const auto upload=premultiplyRasterPixels(*pixels);
            for(int row=0;row<image->height;++row)
                std::copy_n(upload.data()+std::size_t(row)*image->width*4,image->width*4,padded.data()+std::size_t(row)*512*4);
        }
        wgpu::TexelCopyTextureInfo destination{};destination.texture=imageTexture;destination.origin={0,0,uint32_t(layer)};
        wgpu::TexelCopyBufferLayout layout{};layout.bytesPerRow=512*4;layout.rowsPerImage=512;
        const wgpu::Extent3D size{512,512,1};device.GetQueue().WriteTexture(&destination,padded.data(),padded.size(),&layout,&size);
        ++layer;
    }
    cachedImages=std::move(ids);
}

void Renderer::State::buildInstances() {
    view = camera.visibleWorld({cssWidth, cssHeight});
    origin = camera.position();
    stats.visible = buildDrawList(document, view, editor.activeMode(), drawItems);
    addSelectionOutlines();
    uploadImages();

    instances.clear();
    pathVertices.clear();
    instances.reserve(drawItems.size());
    const auto box = [this](const Rect& r, float out[4]) {
        out[0] = float(r.x + r.w / 2.0 - origin.x);
        out[1] = float(r.y + r.h / 2.0 - origin.y);
        out[2] = float(r.w / 2.0);
        out[3] = float(r.h / 2.0);
    };
    const auto radii = [](const std::array<double, 4>& r, float out[4]) {
        for (std::size_t i = 0; i < 4; ++i) out[i] = float(r[i]);
    };
    for (const DrawItem& item : drawItems) {
        GpuItem gpu{};
        gpu.quad[0] = float(item.quad.x - origin.x);
        gpu.quad[1] = float(item.quad.y - origin.y);
        gpu.quad[2] = float(item.quad.w);
        gpu.quad[3] = float(item.quad.h);
        gpu.color[0] = item.color.r;
        gpu.color[1] = item.color.g;
        gpu.color[2] = item.color.b;
        gpu.color[3] = item.color.a;
        gpu.clip[0] = float(item.clip.x - origin.x);
        gpu.clip[1] = float(item.clip.y - origin.y);
        gpu.clip[2] = float(item.clip.x + item.clip.w - origin.x);
        gpu.clip[3] = float(item.clip.y + item.clip.h - origin.y);
        box(item.a.rect, gpu.aRect);
        if (item.kind == DrawKind::Glyph || item.kind == DrawKind::Image) {
            gpu.aRect[0] = float(item.a.rect.x); gpu.aRect[1] = float(item.a.rect.y);
            gpu.aRect[2] = float(item.a.rect.w); gpu.aRect[3] = float(item.a.rect.h);
        }
        if (item.kind == DrawKind::Glyph && item.a.rect.w > 0 && item.a.rect.h > 0) {
            // Grow into the transparent atlas padding so filtering can spread ink past its tight box.
            const double pad = double(kTextAtlasPadding) / kTextAtlasSize;
            const double worldPad = pad * item.quad.w / item.a.rect.w;
            gpu.quad[0] -= float(worldPad); gpu.quad[1] -= float(worldPad);
            gpu.quad[2] += float(2 * worldPad); gpu.quad[3] += float(2 * worldPad);
            gpu.aRect[0] -= float(pad); gpu.aRect[1] -= float(pad);
            gpu.aRect[2] += float(2 * pad); gpu.aRect[3] += float(2 * pad);
        }
        radii(item.a.radii, gpu.aRadii);
        box(item.b.rect, gpu.bRect);
        radii(item.b.radii, gpu.bRadii);
        gpu.params[0] = float(static_cast<int>(item.kind));
        gpu.params[1] = item.ellipse ? 1.0f : 0.0f;
        gpu.params[2] = float(item.sigma);
        gpu.params[3] = float(item.strokeWidth);
        gpu.extra[0] = float(static_cast<int>(item.style));
        gpu.extra[1]=float(item.points.size());
        gpu.extra[2]=float((item.closed?1:0)+(item.filled?2:0));
        gpu.extra[3]=float(pathVertices.size());
        for(const auto p:item.points) pathVertices.push_back({float(p.x-origin.x),float(p.y-origin.y),0,0});
        if(item.image) {
            gpu.extra[0]=float(item.image->width)/512.f;gpu.extra[1]=float(item.image->height)/512.f;
            gpu.extra[3]=float(std::find(cachedImages.begin(),cachedImages.end(),item.image->id)-cachedImages.begin());
        }
        instances.push_back(gpu);
    }

    ensureShapesCapacity(instances.size(),pathVertices.size());
    if(!pathVertices.empty()) device.GetQueue().WriteBuffer(pointsBuffer,0,pathVertices.data(),pathVertices.size()*sizeof(std::array<float,4>));
    if (!instances.empty()) {
        device.GetQueue().WriteBuffer(shapesBuffer, 0, instances.data(), instances.size() * sizeof(GpuItem));
    }
}

void Renderer::State::applyCursor() {
    const std::string_view next = editor.cursor();
    if (next != cursor) {
        cursor = next;
        emscripten::val::global("document")
            .call<emscripten::val>("querySelector", selector)["style"]
            .set("cursor", cursor);
    }
}

void Renderer::State::addSelectionOutlines() {
    const Color color = toSrgb(kSelectionColor);
    if (const auto& marquee = editor.marquee()) {
        Color tint = color;
        tint.a = 0.12f;
        drawItems.push_back({DrawKind::Fill, false, *marquee, {*marquee, {}}, {}, tint, kNoClip});
        drawItems.push_back({DrawKind::Outline, false, *marquee, {*marquee, {}}, {}, color, kNoClip});
    }
    const Id& mode = editor.activeMode();
    const Color componentColor = toSrgb(kComponentColor);
    const auto colorOf = [&](const Id& id) {
        const Node* node = document.view(id);
        const bool component = node && (node->type == NodeType::Component || node->type == NodeType::Instance ||
                                        document.ownerOf(id) != id);
        return component ? componentColor : color;
    };
    for (const Id& id : editor.selection()) {
        if (const auto bounds = worldBounds(document, id, mode)) {
            const auto* node=document.view(id);
            if(node && node->type==NodeType::Line) {
                const auto p=lineEndpoints(*node,*bounds); const double width=1/camera.zoom(),r=width/2;
                const Rect quad{bounds->x-r,bounds->y-r,bounds->w+width,bounds->h+width};
                drawItems.push_back({DrawKind::Segment,false,quad,{{p[0].x,p[0].y,0,0},{}},{{p[1].x,p[1].y,0,0},{}},colorOf(id),kNoClip,0,StrokeStyle::Solid,width});
            } else if(node && node->type==NodeType::Path) {
                const double width=1/camera.zoom(),r=width/2;
                DrawItem item;item.kind=DrawKind::Path;item.quad={bounds->x-r,bounds->y-r,bounds->w+width,bounds->h+width};
                item.color=colorOf(id);item.clip=kNoClip;item.strokeWidth=width;item.points=flattenPath(*node,*bounds);item.closed=node->path.closed;
                drawItems.push_back(std::move(item));
            } else drawItems.push_back({DrawKind::Outline, false, *bounds, {*bounds, {}}, {}, colorOf(id), kNoClip});
        }
    }

    const auto bounds = editor.handleBounds();
    if (!bounds || editor.tool() != Tool::Select) {
        return;
    }
    const double outer = Editor::kHandleSizePx / camera.zoom();
    const double inner = outer - 2.0 / camera.zoom();
    const Color white{1.0f, 1.0f, 1.0f, 1.0f};
    const Color handle = editor.selection().size() == 1 ? colorOf(editor.selection()[0]) : color;
    std::vector<Vec2> points;
    const auto* node=document.view(editor.selection()[0]);
    if(node && node->type==NodeType::Line) {const auto ends=lineEndpoints(*node,*bounds);points.assign(ends.begin(),ends.end());}
    else if(node && node->type==NodeType::Path) {
        const auto path=pathWorldGeometry(*node,*bounds);points=path.points;
        for(std::size_t i=0;i<path.inHandles.size();++i) for(const auto p:{path.inHandles[i],path.outHandles[i]}) {
            const auto anchor=points[i];
            if(std::hypot(p.x-anchor.x,p.y-anchor.y)<1e-12) continue;
            const double width=1/camera.zoom(),radius=width/2;
            const Rect quad{std::min(p.x,anchor.x)-radius,std::min(p.y,anchor.y)-radius,std::abs(p.x-anchor.x)+width,std::abs(p.y-anchor.y)+width};
            drawItems.push_back({DrawKind::Segment,false,quad,{{anchor.x,anchor.y,0,0},{}},{{p.x,p.y,0,0},{}},handle,kNoClip,0,StrokeStyle::Solid,width});
            const Rect border{p.x-outer/2,p.y-outer/2,outer,outer},fill{p.x-inner/2,p.y-inner/2,inner,inner};
            drawItems.push_back({DrawKind::Fill,true,border,{border,{}},{},handle,kNoClip});
            drawItems.push_back({DrawKind::Fill,true,fill,{fill,{}},{},white,kNoClip});
        }
    }
    else for(double x:{bounds->x,bounds->x+bounds->w}) for(double y:{bounds->y,bounds->y+bounds->h}) points.push_back({x,y});
    for (const auto& point:points) {
            const auto [x,y]=point;
            const Rect border{x - outer / 2, y - outer / 2, outer, outer};
            const Rect fill{x - inner / 2, y - inner / 2, inner, inner};
            drawItems.push_back({DrawKind::Fill, false, border, {border, {}}, {}, handle, kNoClip});
            drawItems.push_back({DrawKind::Fill, false, fill, {fill, {}}, {}, white, kNoClip});
    }
}

bool Renderer::State::frame(double time) {
    if (!running) {
        looping = false;
        return false;
    }

    if (lastTime > 0.0) {
        stats.frameMs += (time - lastTime - stats.frameMs) * kStatsSmoothing;
    }
    lastTime = time;
    const double cpuStart = emscripten_get_now();
    stats.total = document.size();

    applyCursor();
    if (!resizeIfNeeded()) {
        return true;
    }

    wgpu::SurfaceTexture surfaceTexture;
    surface.GetCurrentTexture(&surfaceTexture);
    if (surfaceTexture.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal &&
        surfaceTexture.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal) {
        return true;
    }

    writeFrameUniform();
    buildInstances();

    wgpu::RenderPassColorAttachment color{};
    color.view = surfaceTexture.texture.CreateView();
    color.loadOp = wgpu::LoadOp::Clear;
    color.storeOp = wgpu::StoreOp::Store;
    color.clearValue = {0.12, 0.12, 0.13, 1.0};

    wgpu::RenderPassDescriptor passDesc{};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &color;

    wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
    wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&passDesc);
    pass.SetBindGroup(0, bindGroup);
    pass.SetPipeline(gridPipeline);
    pass.Draw(3);
    if (!instances.empty()) {
        pass.SetPipeline(shapesPipeline);
        pass.Draw(4, static_cast<uint32_t>(instances.size()));
    }
    pass.End();

    wgpu::CommandBuffer commands = encoder.Finish();
    device.GetQueue().Submit(1, &commands);

    stats.cpuMs += (emscripten_get_now() - cpuStart - stats.cpuMs) * kStatsSmoothing;
    return true;
}

} // namespace vc::render
