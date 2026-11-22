#pragma once
#include "vc/text.hpp"
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "vc/camera.hpp"
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

/// Parses an array of `{ id, kind, data }` objects, such as a page of the server's op log. `id` is
/// optional and nil when missing. Extra fields are ignored. Returns nothing if any entry is malformed.
[[nodiscard]] std::optional<std::vector<Op>> opsFromJson(std::string_view data);

/// A node for the inspector: every stored field (defaults included), `fill.hex` when the fill is a
/// literal color, `box` (its laid-out rect relative to the parent in `mode`), and `inFlow` when an
/// auto-layout parent places it.
[[nodiscard]] std::string inspectNodeJson(const Document& document, const Node& node, const Id& mode);

/// Parses a patch in the same shape `SetProps` uses on the wire.
[[nodiscard]] std::optional<NodePatch> patchFromJson(std::string_view data);
[[nodiscard]] std::optional<ComponentDefinition> componentDefinitionFromJson(std::string_view data);

/// A color as `{ l, c, h, a }`, the shape fills use.
[[nodiscard]] std::string colorToJson(const Oklch& color);

/// Rows for a layers panel: `{ id, name, type, depth, visible }`, topmost layer first, children under their parent.
[[nodiscard]] std::string layersToJson(const Document& document);

/// Names of top-level frames for labels on the canvas: `{ id, name, x, y, w }` where x and y are the
/// frame's top-left corner and w its width, in screen CSS pixels.
[[nodiscard]] std::string frameLabelsToJson(const Document& document, const Camera& camera, const Id& mode);

/// Every node of `document`, parents before children, as a JSON snapshot.
[[nodiscard]] std::string snapshotToJson(const Document& document);

/// The ops that rebuild a snapshot (modes, then tokens, then nodes), in an order `Document::apply`
/// accepts. Reads supported versions 1 through 24, rejecting semantic downgrades.
[[nodiscard]] std::optional<std::vector<OpBody>> snapshotFromJson(std::string_view data);

/// Tokens for the tokens panel, in family order, each with its value `resolved` in `mode` (colors with
/// alpha and hex) and whether anything `referenced` it.
[[nodiscard]] std::string tokensToJson(const Document& document, const Id& mode);
/// Modes in order, the default one flagged `isDefault`.
[[nodiscard]] std::string modesToJson(const Document& document);
/// `{ fonts, fontStacks }` in order, in the snapshot's shape.
[[nodiscard]] std::string fontsToJson(const Document& document);
/// A font or font stack in the shape the ops use.
[[nodiscard]] std::optional<Font> fontFromJson(std::string_view data);
[[nodiscard]] std::optional<FontStack> fontStackFromJson(std::string_view data);
/// A family's files as `[{ face, fixed: { tag: value } }]`.
[[nodiscard]] std::optional<std::vector<FontFile>> fontFilesFromJson(std::string_view data);
/// The registry key of an icon reference given as a JSON string (path form) or object, or nothing.
[[nodiscard]] std::optional<std::string> iconSourceKeyFromJson(std::string_view data);
/// An icon reference as the JSON string or object a node takes, in the shape documents store.
[[nodiscard]] std::string iconToJson(const IconRef& icon);
/// Parse a token or mode definition in the same shape the ops use.
[[nodiscard]] std::optional<Token> tokenFromJson(std::string_view data);
[[nodiscard]] std::optional<Mode> modeFromJson(std::string_view data);

} // namespace vc
