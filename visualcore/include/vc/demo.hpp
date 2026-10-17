#pragma once
#include <cstddef>
#include <cstdint>
#include "vc/math.hpp"
#include "vc/session.hpp"

namespace vc {

/// Adds `count` random frames, rects and ellipses inside `area`, some of them nested in frames.
/// Applied as remote ops: not undoable and never sent to the server. Same seed, same nodes.
void addRandomNodes(Session& session, std::size_t count, const Rect& area, std::uint64_t seed);

} // namespace vc
