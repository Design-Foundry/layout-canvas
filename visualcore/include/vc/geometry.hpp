#pragma once
#include <optional>
#include <unordered_set>
#include "vc/document.hpp"
#include "vc/math.hpp"

namespace vc {

/// Canonical left-to-right endpoints inside the supplied local or world box.
[[nodiscard]] std::array<Vec2,2> lineEndpoints(const Node& node,const Rect& box);
/// Normalizes either endpoint order into a nonnegative box and negative-slope flag.
[[nodiscard]] NodePatch lineGeometry(Vec2 a,Vec2 b);

/// Maps normalized path vertices into the supplied local or world box.
[[nodiscard]] std::vector<Vec2> pathPoints(const Node& node, const Rect& box);
/// Normalizes vertices into a nonnegative bounding box; invalid input produces an invalid path patch.
[[nodiscard]] NodePatch pathGeometry(const std::vector<Vec2>& points, bool closed = false);
/// Normalizes the box around vertices AND controls, keeping filled and closed semantics.
[[nodiscard]] NodePatch pathGeometry(const Path& world);
[[nodiscard]] Path pathWorldGeometry(const Node& node,const Rect& box);
/// Adaptive cubic flattening, at most 4096 vertices. Closed paths have implicit final closure.
[[nodiscard]] std::vector<Vec2> flattenPath(const Node& node,const Rect& box,double tolerance = .25);

/// The node's rectangle in world space after auto layout, or nothing if it doesn't exist.
[[nodiscard]] std::optional<Rect> worldBounds(const Document& document, const Id& id,
                                              const Id& mode = kDefaultModeId);

/// The topmost visible, unlocked node under `world`, honoring frame clipping. With `framesOnly`, only
/// frames are considered. Returns a nil id when nothing is hit.
[[nodiscard]] Id hitTest(const Document& document, Vec2 world, bool framesOnly = false,
                         const Id& mode = kDefaultModeId, double tolerance = 4.0,
                         const Id& root = {}, bool includeLocked = false, Vec2 scroll = {});

/// Positive right/bottom overflow of visible layout boxes, respecting descendant clips.
/// Root clipping does not constrain its own scroll range; paint bleed is excluded.
[[nodiscard]] Vec2 prototypeScrollLimit(const Document& document, const Id& root,
                                       const Id& mode = kDefaultModeId);

/// The deepest visible, unlocked frame under `world`, skipping `excluded` nodes and everything inside
/// them. Returns a nil id for the canvas itself.
[[nodiscard]] Id frameAt(const Document& document, Vec2 world, const Id& mode, const std::unordered_set<Id>& excluded);

} // namespace vc
