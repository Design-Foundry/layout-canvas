#pragma once
#include <unordered_map>
#include "vc/id.hpp"
#include "vc/math.hpp"

namespace vc {

class Document;

/// Every node's box relative to its parent after auto layout, hug and fill sizing and min/max
/// limits are applied, in mode `mode` (gaps, paddings and limits may be tokens).
[[nodiscard]] std::unordered_map<Id, Rect> computeLayout(const Document& document, const Id& mode);

} // namespace vc
