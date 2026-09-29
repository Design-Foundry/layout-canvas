#pragma once
#include <memory>
#include <string>

namespace vc::render {
class Renderer {
public:
    explicit Renderer(std::string canvasSelector);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void start();

private:
    struct State;
    std::unique_ptr<State> state_;
};
}  // namespace vc::render
