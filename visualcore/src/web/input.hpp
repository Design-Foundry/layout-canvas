#pragma once
#include "vc/viewport_controller.hpp"

namespace vc::web {

/// Forwards mouse and wheel events from the element matching `canvasSelector` to `controller`.
/// The controller must outlive the page.
void attachInput(const char* canvasSelector, ViewportController& controller);

} // namespace vc::web
