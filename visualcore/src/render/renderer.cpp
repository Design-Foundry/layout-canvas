#include "renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <webgpu/webgpu_cpp.h>

#include "shaders.hpp"

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

struct RectInstance {
    float pos[2];
    float size[2];
    float color[4];
};

constexpr double kMinGridSpacingPx = 8.0;
constexpr double kGridFadeEndPx = 32.0;
constexpr std::size_t kInitialRectCapacity = 1024;
constexpr double kStatsSmoothing = 0.1;

double positiveMod(double value, double modulus) {
    const double r = std::fmod(value, modulus);
    return r < 0.0 ? r + modulus : r;
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
    const Scene& scene;

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
    wgpu::RenderPipeline rectsPipeline;
    wgpu::Buffer frameBuffer;
    wgpu::Buffer rectsBuffer;
    std::size_t rectsCapacity = 0;
    wgpu::BindGroup bindGroup;

    std::vector<Shape> visible;
    std::vector<RectInstance> instances;

    bool running = false;
    bool looping = false;
    double lastTime = 0.0;
    FrameStats stats;

    State(std::string selector, const Camera& camera, const Scene& scene)
        : selector(std::move(selector)), camera(camera), scene(scene) {}

    void requestAdapter();
    void requestDevice();
    void onDevice();
    void createPipelines();
    void ensureRectsCapacity(std::size_t count);
    void startLoop();
    bool resizeIfNeeded();
    void writeFrameUniform();
    void buildInstances();
    bool frame(double time);
};

Renderer::Renderer(std::string canvasSelector, const Camera& camera, const Scene& scene)
    : state_(std::make_unique<State>(std::move(canvasSelector), camera, scene)) {}

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
    ensureRectsCapacity(kInitialRectCapacity);

    if (running) {
        startLoop();
    }
}

void Renderer::State::createPipelines() {
    wgpu::BindGroupLayoutEntry entries[2]{};
    entries[0].binding = 0;
    entries[0].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
    entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
    entries[1].binding = 1;
    entries[1].visibility = wgpu::ShaderStage::Vertex;
    entries[1].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;

    wgpu::BindGroupLayoutDescriptor layoutDesc{};
    layoutDesc.entryCount = 2;
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
    rectsPipeline = createPipeline(kRectsWgsl, wgpu::PrimitiveTopology::TriangleStrip);

    wgpu::BufferDescriptor frameDesc{};
    frameDesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    frameDesc.size = sizeof(FrameUniform);
    frameBuffer = device.CreateBuffer(&frameDesc);
}

void Renderer::State::ensureRectsCapacity(std::size_t count) {
    if (count <= rectsCapacity) {
        return;
    }
    rectsCapacity = std::max(count, rectsCapacity * 2);

    wgpu::BufferDescriptor desc{};
    desc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
    desc.size = rectsCapacity * sizeof(RectInstance);
    rectsBuffer = device.CreateBuffer(&desc);

    wgpu::BindGroupEntry entries[2]{};
    entries[0].binding = 0;
    entries[0].buffer = frameBuffer;
    entries[1].binding = 1;
    entries[1].buffer = rectsBuffer;

    wgpu::BindGroupDescriptor groupDesc{};
    groupDesc.layout = bindGroupLayout;
    groupDesc.entryCount = 2;
    groupDesc.entries = entries;
    bindGroup = device.CreateBindGroup(&groupDesc);
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
    const double minor = std::pow(10.0, std::ceil(std::log10(kMinGridSpacingPx / zoom)));
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

void Renderer::State::buildInstances() {
    scene.visible(camera.visibleWorld({cssWidth, cssHeight}), visible);

    const Vec2 origin = camera.position();
    instances.clear();
    instances.reserve(visible.size());
    for (const Shape& shape : visible) {
        const Rect& r = shape.bounds;
        instances.push_back({
            {float(r.x - origin.x), float(r.y - origin.y)},
            {float(r.w), float(r.h)},
            {shape.fill.r, shape.fill.g, shape.fill.b, shape.fill.a},
        });
    }

    ensureRectsCapacity(instances.size());
    if (!instances.empty()) {
        device.GetQueue().WriteBuffer(rectsBuffer, 0, instances.data(),
                                      instances.size() * sizeof(RectInstance));
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
    stats.total = scene.shapes().size();

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
        pass.SetPipeline(rectsPipeline);
        pass.Draw(4, static_cast<uint32_t>(instances.size()));
    }
    pass.End();

    wgpu::CommandBuffer commands = encoder.Finish();
    device.GetQueue().Submit(1, &commands);

    stats.cpuMs += (emscripten_get_now() - cpuStart - stats.cpuMs) * kStatsSmoothing;
    stats.visible = instances.size();
    return true;
}

} // namespace vc::render
