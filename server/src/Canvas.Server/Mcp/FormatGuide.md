# Canvas document format

A document is a tree of nodes plus tokens and modes. Edit it with `apply_ops`; read it with `get_document`, `get_nodes` and `get_layout`.

## Ids and order

- Every node, token and mode has a UUID id. In `apply_ops` write a placeholder such as `"$card"` instead; it becomes one new UUID across the batch.
- `parent` is the id of the containing node; omit it for top-level frames.
- `order` sorts siblings as a string (fractional index: `a0`, `a1`, ... `a9`, `aA` ... `aZ`, `aa` ... `az`). Later order paints on top and comes later in auto layout. In `apply_ops`, leave it out to append after existing siblings.
- Create parents before children, and a component's layers before its definition.

## Nodes

`{ "id", "type", "parent", "order", "name", "x", "y", "w", "h", ...properties }`

Types: `frame`, `component`, `instance`, `rect`, `ellipse`, `text`, `line`, `path`, `image`. Frames and components hold children.

Common properties (all optional):
- `x`, `y`: position relative to the parent; ignored for children placed by auto layout unless `absolute: true`.
- `w`, `h`: size, at least 0.
- `fill`: color; `fillVisible: false` hides it, and `fill: null` does the same. Frames and components have no fill unless you set `fill`; other nodes without `fill` are black.
- `opacity`: 0 to 1, or a token.
- `cornerRadius`: length; `cornerRadii`: `[topStart, topEnd, bottomEnd, bottomStart]`, each a length or null.
- `stroke`: `{ "width", "style": "solid" | "dashed" | "dotted" | "double" | "none", "color", "align": "inside" | "center" | "outside", "sides": [top, end, bottom, start] }` (`sides` optional, each a length or null), or a border token.
- `shadows`: `[{ "x", "y", "blur", "spread", "color", "inset", "visible" }]` or a shadow token.
- `visible`, `locked`, `clipContent`: booleans.
- `widthSizing`, `heightSizing`: `fixed` (default), `hug` (fit content) or `fill` (take the free space in an auto-layout parent).
- `limits`: `{ "minWidth", "maxWidth", "minHeight", "maxHeight" }` lengths.

Values:
- Color: `{ "l": 0-1, "c": >= 0, "h": 0-360, "a": 0-1 }` (OKLCH) or `{ "token": "<color token id>" }`.
- Length: a number or `{ "token": "<dimension token id>" }`.

### Auto layout

`"layout": { "type": "row" | "column" | "none", "gap": length, "padding": [top, end, bottom, start], "justify": "start" | "center" | "end" | "spaceBetween" | "spaceAround" | "spaceEvenly", "align": "start" | "center" | "end" | "stretch" }`

All five fields are required when `layout` is present. Typical card: a `column` frame with `heightSizing: "hug"`, padding and gap; children with `widthSizing: "fill"` stretch across. A flexible spacer is an empty frame with `widthSizing: "fill"` (in a row) and `fillVisible: false`.

### Text

`"text": { "content", "font", "fontSize", "lineHeight", "letterSpacing", "align" }`, all required.
- `font`: `inter-regular-v1`, `inter-bold-v1`, `inter-oblique-v1` or `inter-bold-oblique-v1`.
- `fontSize` 1 to 512; `lineHeight` 1 to 4 (a multiplier); `letterSpacing` 0 to 128; `align`: `left`, `center` or `right`.
- Content: Latin text, accents included; `\n` for explicit lines.
- `textWrap: true` wraps at the node's width (use `fixed` or `fill` width with `hug` height).
- `typography: "<typography token id>"` binds shared metrics; keep `text` metrics equal to the token's.
- Color comes from `fill`. Text takes no stroke, shadows, radius or layout.
- Size text with `widthSizing: "hug", heightSizing: "hug"` unless it wraps.

### Lines and paths

- `line`: a segment from the top-left to the bottom-right corner of its box, or bottom-left to top-right with `lineReverse: true`. A horizontal line has `h: 0`. Thickness is `lineWidth`, color is `fill`.
- `path`: `"path": { "points": [{ "x", "y" }], "closed", "filled", "inHandles", "outHandles" }`. Points and handles are normalized to the node's box (0 to 1). At most 64 points, one contour. `inHandles`/`outHandles` (one per point) make cubic curves; a handle equal to its point is a sharp corner. `filled` needs `closed`. Stroke width is `lineWidth`, color is `fill`. Lines and paths use fixed sizing.

### Prototype viewport scrolling

- An authored top-level `frame` can set `prototypeOverflow: "none" | "vertical" | "horizontal" | "both"`. The default is `"none"`; use it in `SetProps` to remove scrolling. A non-default value requires snapshot version 19 or later.
- The frame's `w` and `h` define its viewport. Place child content beyond its bounds to enable scrolling on the selected axes during Play. The same frame setting applies when it is presented as a screen or opened as an overlay.
- Playback clips scrolled children to the viewport and keeps drawing and hit testing aligned. Scrolling is temporary playback state; it does not change child positions or append document operations.
- Nested frames, components, instances, copied layers, component overrides and variant rules cannot set this field, including an explicit `"none"`. Clear scrolling before nesting a configured frame or changing its type.
- This field does not implement fixed or sticky child positioning. Portable component files refuse document-local prototype metadata.

### Ordered prototype interactions

- `interactions` is an ordered array of up to 16 actions and requires native snapshot version 20 or later. Each entry has `trigger: "click" | "hover" | "press" | "delay"` and `action: "navigate" | "back" | "open-overlay" | "close-overlay"`.
- Navigate and Open overlay require a root-frame `target`. They accept the same native transition, duration and overlay placement settings as the legacy `interaction` field. Back and Close overlay have no target or transition settings.
- A Delay entry requires integer `delayMs` from 1 to 60000. Other triggers refuse this field. An entry with `action: "none"`, unknown fields or unsupported trigger values is refused; clear the list with `interactions: []` instead.
- Existing `interaction` documents retain their click behavior and minimum snapshot version. Setting `interactions` clears a legacy `interaction`; setting legacy `interaction` clears the list. Do not send both with active values. The explicit empty list is also an independent override that masks inherited prototype actions.
- Lists belong to source layers or independent component-layer overrides. Raw instance metadata and variant rules cannot set them. Every listed destination is validated, including entries that would lose priority at runtime; delete, nesting and type-change safeguards retain all those references.
- Pointer events resolve the deepest ancestor with a matching trigger, then its first entry for that trigger. Later entries for the same trigger are shadowed, not chained. Hover fires on pointer entry, excluding touch and dragging. Primary pointer-down fires Press and consumes the corresponding Click; a drag also cancels Click.
- Delay uses the first matching entry per node once per visit, traversing visible nodes in paint order and executing at most one due action per playback update. The clock pauses during transitions; only the active base screen or top modal runs timers. Hidden, clipped and offscreen nodes do not fire; scrolling them into view can make an already due timer eligible.
- Entry or reveal through Back/Close resets the active screen's timer clock. Viewport resizing restarts the full delay epoch; incoming document edits cancel pending timers until explicit navigation or Restart. These timers are temporary playback state and produce no document operations. See [prototyping usage](../../../../docs/prototyping.md) for the full lifecycle.
- Portable component files refuse document-local lists even when empty, and snapshots older than version 20 refuse list metadata rather than silently dropping it. These actions do not add external URL navigation, conditions or variables.

### Component playback state and Smart Animate

Snapshot 21 adds `action: "change-state"` to legacy and ordered interactions. It
requires `values: { "axis": "choice" }` with 1–8 nonempty string entries (axis
names up to 64 UTF-8 bytes, choices up to 128), no `target`, and only Instant,
`dissolve` or `smart-animate` transitions. Existing duration bounds apply. Other
actions refuse `values`; Navigate also accepts Smart, while Open overlay does not.

The action resolves the nearest authored instance in the hotspot's ancestry and
checks every choice against that component's declared variant axes. Invalid axes,
choices, missing instance context and derived nested instances produce no state
change. Syntactically valid relative actions may be stored on reusable source
layers before they have a playback context. These actions do not set arbitrary
properties, text properties or instance swaps.

Play uses an isolated document copy. Instance states persist through navigation,
Back and overlays, and reset on Restart, Exit or incoming document edits. Playback
does not alter source values, undo history or the outgoing operation log.

Smart Animate requires matching expanded node/paint identities, paint order and
topology. Compatible geometry, paint color and opacity interpolate; glyph/image
UVs remain fixed. Changed text, image identity/crop, missing/reordered paints and
incompatible paths use full-scene dissolve. Distinct screen roots have distinct
identities and therefore use that fallback. Portable component files still refuse
prototype metadata. See the usage guide for the supported scope.

## Tokens

`{ "id", "type", "identifier", "order", "value" | "source", "modifiers" }`

- `identifier`: dot-separated lowercase kebab-case segments that each start with a letter, unique per type: `color.text-subtle`, `space.lg`.
- `type` and `value`:
  - `color`: `{ "l", "c", "h" }`
  - `dimension`: `{ "value", "unit": "px" | "rem" | "em" | "pt" }`
  - `opacity`, `ratio`: `{ "value" }`; `duration`: `{ "value", "unit": "ms" | "s" }`
  - `typography`: `{ "font", "fontSize", "lineHeight", "letterSpacing", "align" }`
  - `border`: `{ "width": "<dimension token id>", "style", "color": "<color token id>" }`
  - `shadow`: `{ "layers": [{ "offsetX", "offsetY", "blur", "spread", "inset", "color": "<color token id>", "colorModifiers": [...] }] }`
- Alias instead of a value: `"source": { "<mode id>": "<token id of the same type>" }`. The default mode is required.
- Color modifiers: `{ "channel": "l" | "c" | "h" | "alpha", "kind": "set" | "delta" | "factor", "value": number }` or `{ "mix": "<color token id>", "amount": 0-1 }`. A shadow color with 8% alpha: `"colorModifiers": [{ "channel": "alpha", "kind": "set", "value": 0.08 }]`.

## Modes

`{ "id", "name", "order" }`. The default mode is `00000000-0000-0000-0000-000000000001` and always exists. A light/dark pair: primitive tokens for each mode, then semantic aliases with a source per mode.

## Components

- Main component: a `component` node with children. After the children exist, set its definition with `SetComponentDefinition`:
  `{ "texts": [{ "name", "target": "<text layer id>", "default" }], "properties": [{ "name", "target", "default": bool }], "axes": [{ "name", "values": [...], "default" }], "rules": [{ "when": { "<axis>": "<value>" }, "target": "<layer id>", "patch": { ... } }], "swaps": [...] }`
  - Names and axis values: lowercase kebab-case.
  - `properties` show or hide their target.
  - `rules` patch a layer when the axis values match: fill, stroke, opacity, size, layout, path, `textStyle` (partial text metrics), and similar. Rules cannot change name, visibility, sizing modes or type, nor `text` on a layer a text property controls.
- Instance: `{ "type": "instance", "component": "<main id>", "componentValues": { "<property or axis>": value } }`. Its layers come from the main; give the instance its own `w`, `h` and sizing. Without a `name` it takes the main's.
- Per-instance changes beyond properties: `SetOverride` with `node` = the main's layer id.

## Ops (`kind` and `data`)

- `CreateNode` `{ "node": {...} }`
- `SetProps` `{ "id", "patch": { only the properties to change } }`
- `MoveNode` `{ "id", "parent", "order" }`
- `DeleteNode` `{ "id" }`: children first; a node a definition targets cannot be deleted.
- `SetComponentDefinition` `{ "id": "<component id>", "definition": {...} }`
- `SetOverride` `{ "id": "<instance id>", "node": "<main layer id>", "patch": {...} }`; an empty patch removes the override.
- `CreateToken` / `ReplaceToken` `{ "token": {...} }`, `DeleteToken` `{ "id" }`
- `CreateMode` / `ReplaceMode` `{ "mode": {...} }`, `DeleteMode` `{ "id" }`

A refused op reports `path` and `message`. Later ops that depend on it (children of a refused frame, for example) are refused too; fix the first error and resend those.

## Checking the result

Call `get_layout` on the frame you built: confirm sizes and positions, and look for `overflows`, which marks layers spilling outside their parent (clipped text, children wider than a fixed parent).

### Named prototype flows

Snapshot 22 adds `prototypeFlowName`, a single-line string up to 128 UTF-8 bytes,
on authored top-level frames only. Empty string clears it. The label is independent
of `prototypeStart`; disabling a start preserves its name. Only marked starts show
the flow label in the starting-screen picker. Renaming the frame does not rename
the flow. Root-only constraints apply even for explicit empty names. Overrides and
variant rules cannot set the field. Old snapshots refuse explicit flow metadata,
and portable component packages refuse it instead of stripping it. SetProps uses
normal reversible operations; Play never writes flow metadata.

Full-document `.canvas.json` files use the `canvas-document/1` envelope with name
and native snapshot. They preserve flow names, all interactions and inline images.
Reopening uses the existing atomic document import (2 MiB snapshot limit), creating
a separate document on the configured server. This is local file delivery, not
hosted sharing or a standalone HTML player. Component files keep their narrower
portable contract.
