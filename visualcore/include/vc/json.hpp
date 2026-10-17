#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "vc/document.hpp"
#include "vc/op.hpp"

namespace vc {

/// The op kind as the server stores it, such as "CreateNode".
[[nodiscard]] std::string_view opKind(const OpBody& op);

/// Payload of an op as JSON. Together with `opKind` this is what the server's op log holds.
[[nodiscard]] std::string opToJson(const OpBody& op);

/// Parses an op from the server log. Returns nothing for unknown kinds or malformed data.
[[nodiscard]] std::optional<OpBody> opFromJson(std::string_view kind, std::string_view data);

/// Ops ready to post to the server: an array of `{ id, kind, data }`.
[[nodiscard]] std::string opsToJson(const std::vector<Op>& ops);

/// Parses an array of `{ kind, data }` objects, such as a page of the server's op log. Extra fields are
/// ignored. Returns nothing if any entry is malformed.
[[nodiscard]] std::optional<std::vector<OpBody>> opsFromJson(std::string_view data);

/// Rows for a layers panel: `{ id, name, type, depth }`, topmost layer first, children under their parent.
[[nodiscard]] std::string layersToJson(const Document& document);

/// Every node of `document`, parents before children, as a JSON snapshot.
[[nodiscard]] std::string snapshotToJson(const Document& document);

/// The create ops that rebuild a snapshot, in an order `Document::apply` accepts.
[[nodiscard]] std::optional<std::vector<CreateNode>> snapshotFromJson(std::string_view data);

} // namespace vc
