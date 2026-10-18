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

/// A node for the inspector: the stored fields plus `fill.hex` when the fill is a literal color.
[[nodiscard]] std::string inspectNodeJson(const Node& node);

/// Parses a patch in the same shape `SetProps` uses on the wire.
[[nodiscard]] std::optional<NodePatch> patchFromJson(std::string_view data);

/// A color as `{ l, c, h, a }`, the shape fills use.
[[nodiscard]] std::string colorToJson(const Oklch& color);

/// Rows for a layers panel: `{ id, name, type, depth, visible }`, topmost layer first, children under their parent.
[[nodiscard]] std::string layersToJson(const Document& document);

/// Every node of `document`, parents before children, as a JSON snapshot.
[[nodiscard]] std::string snapshotToJson(const Document& document);

/// The ops that rebuild a snapshot (modes, then tokens, then nodes), in an order `Document::apply`
/// accepts. Reads versions 1 and 2.
[[nodiscard]] std::optional<std::vector<OpBody>> snapshotFromJson(std::string_view data);

/// Tokens for the tokens panel, in family order, each with its value `resolved` in `mode` (colors with
/// alpha and hex) and whether anything `referenced` it.
[[nodiscard]] std::string tokensToJson(const Document& document, const Id& mode);
/// Modes in order, the default one flagged `isDefault`.
[[nodiscard]] std::string modesToJson(const Document& document);
/// Parse a token or mode definition in the same shape the ops use.
[[nodiscard]] std::optional<Token> tokenFromJson(std::string_view data);
[[nodiscard]] std::optional<Mode> modeFromJson(std::string_view data);

} // namespace vc
