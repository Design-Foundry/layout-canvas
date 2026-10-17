#pragma once
#include "vc/editor.hpp"

namespace vc::web {

/// Forwards mouse and wheel events from the element matching `canvasSelector` to `editor`.
/// The editor must outlive the page.
void attachInput(const char* canvasSelector, Editor& editor);

} // namespace vc::web
