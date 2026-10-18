#pragma once
#include <optional>
#include "vc/document.hpp"
#include "vc/math.hpp"

namespace vc {

/// The node's rectangle in world space after auto layout, or nothing if it doesn't exist.
[[nodiscard]] std::optional<Rect> worldBounds(const Document& document, const Id& id,
                                              const Id& mode = kDefaultModeId);

/// The topmost visible, unlocked node under `world`, honoring frame clipping. With `framesOnly`, only
/// frames are considered. Returns a nil id when nothing is hit.
[[nodiscard]] Id hitTest(const Document& document, Vec2 world, bool framesOnly = false,
                         const Id& mode = kDefaultModeId);

} // namespace vc
