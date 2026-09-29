#include "renderer.hpp"

#include <cstdio>
#include <utility>

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <webgpu/webgpu_cpp.h>

namespace vc::render {

struct Renderer::State {
    std::string selector;
    wgpu::Instance instance;
    wgpu::Surface surface;
    wgpu::Adapter adapter;
    wgpu::Device device;
    wgpu::TextureFormat format = wgpu::TextureFormat::Undefined;
    uint32_t width = 0;
    uint32_t height = 0;

    void requestAdapter();
    void requestDevice();
    void onDevice();
    bool frame();
};

Renderer::Renderer(std::string canvasSelector) : state_(std::make_unique<State>()) {
    state_->selector = std::move(canvasSelector);
}

Renderer::~Renderer() = default;

void Renderer::start() {
    state_->instance = wgpu::CreateInstance(nullptr);

    wgpu::EmscriptenSurfaceSourceCanvasHTMLSelector source{};
    source.selector = state_->selector.c_str();

    wgpu::SurfaceDescriptor surfaceDesc{};
    surfaceDesc.nextInChain = &source;

    state_->surface = state_->instance.CreateSurface(&surfaceDesc);

    state_->requestAdapter();
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
    double cssWidth = 0;
    double cssHeight = 0;
    emscripten_get_element_css_size(selector.c_str(), &cssWidth, &cssHeight);

    const double dpr = emscripten_get_device_pixel_ratio();
    width = static_cast<uint32_t>(cssWidth * dpr);
    height = static_cast<uint32_t>(cssHeight * dpr);
    emscripten_set_canvas_element_size(selector.c_str(), int(width), int(height));

    wgpu::SurfaceCapabilities caps;
    surface.GetCapabilities(adapter, &caps);
    format = caps.formats[0];

    wgpu::SurfaceConfiguration config{};
    config.device = device;
    config.format = format;
    config.width = width;
    config.height = height;
    surface.Configure(&config);

    emscripten_request_animation_frame_loop(
        [](double, void* user) { return static_cast<State*>(user)->frame(); }, this);
}

bool Renderer::State::frame() {
    wgpu::SurfaceTexture surfaceTexture;
    surface.GetCurrentTexture(&surfaceTexture);
    if (surfaceTexture.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal &&
        surfaceTexture.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal) {
        return true;
    }

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
    pass.End();

    wgpu::CommandBuffer commands = encoder.Finish();
    device.GetQueue().Submit(1, &commands);

    return true;
}

}  // namespace vc::render