# Editor capabilities and limits

The editor stores these features in native document operations and snapshots;
canvas rendering, Undo/Redo, component inheritance and portable files use the same
contracts. This is an editor foundation increment, not a claim of full Figma parity.

| Area | Available | Current limits |
| --- | --- | --- |
| Components | Main components, linked instances, boolean/text/swap properties, variant axes, per-field rule editing, independent overrides, guarded main switching, reset/detach | Some edits that would invalidate existing override addresses are refused |
| Text | On-canvas and inspector editing, multiline/wrapping, Hug sizing, metric overrides and declared string properties | A bounded single run of UTF-8 text with per-character font fallback through the stack and the bundled generic faces; uncovered characters draw the missing-glyph box; no rich runs, right-to-left layout or glyph-level canvas caret |
| Typography | Fonts as families with capabilities, font stacks with a generic fallback, bundled Inter, Google Fonts families pinned to a google/fonts commit and served by the Canvas server, HarfBuzz shaping with each font's kerning and default features, weight, italic, other axes and features offered from each font's capabilities, per-font availability switches to preview fallbacks (canvas and prototype preview, not saved), local shared typography tokens, per-field instance inheritance | Literal shared style metrics; no aliases/modes for composite typography |
| Vectors | Rectangles, ellipses, editable round-cap lines, cubic paths, anchor/control editing, even-odd fills, color tokens and opacity | Up to 64 anchors; one contour and common fill/stroke paint; no boolean operations or cap/join choices |
| Drawing | Pen click/drag, Pencil, five-sided Polygon, five-point Star, Shift constraints, Alt-centered shapes, gesture cancellation | Pencil reduces to 64 samples and does not retain pressure or smooth; polygons/stars become editable paths without parametric controls |
| Images | Local static PNG/JPEG import, native RGBA pixels, SHA-256 identity, fixed geometry, contain/cover/stretch, normalized crop, opacity and clipping | 4 MB input, 4096 px source side, explicit downsample to 512 px, eight identities and 1,400,000 encoded pixel characters per document; no original-resolution recovery, animation or effects |
| Exchange | Portable `canvas-component/1` files with native validation and dependent mains/tokens; atomic import into a new document | 2 MB files; malformed/unsupported data refuses instead of silent substitution |
| Prototyping | Root-frame screens, starting screen selection, Navigate/Back/Open/Close overlay, positioned modals, scene-composited dissolve/slide/push, preserved outgoing viewports, reduced motion, scrollable screen/modal viewports, inherited hotspot overrides, ordered click/hover/press/delay interactions, isolated nested component states, hierarchical cross-screen Smart Animate and native playback | No partial-tree matching, fixed/sticky or nested scroll containers, variables, URLs or hosted sharing; directional transitions use dissolve across mismatched viewport shapes; portable files refuse document-local links |

## Component parts

A layer inside a main component can carry a part, a lowercase kebab-case name
(up to 64 characters) that is unique within that component. The inspector shows a
Part field for stored layers inside a main component, including the component root.
Parts are optional, need snapshot 23 and are refused on layers outside a component.
Expanded copies in instances expose the part of their source layer, but instances,
overrides and variant rules cannot change it. Variant rules keep targeting layer ids,
so renaming a part never breaks a rule. Detaching an instance clears the parts of its
copies. Dragging a layer into another component keeps its part unless that component
already uses it, and dragging it out of every component clears it; both are one undo
step. SetProps, CreateNode and MoveNode operations refuse duplicate parts.

## Notes

Any layer can carry notes: plain text of up to 2048 characters and 4096 UTF-8 bytes,
newlines allowed, never rendered. The inspector shows a Notes field for the selected
layer, except for copies inside instances. Notes survive duplicate, clone, undo, redo and
export. They need snapshot 26 and are written only when a layer has some. Overrides and
variant rules cannot set them.

## Icons

An `icon` layer draws a glyph of the built-in Material Symbols set (outlined, rounded or sharp; FILL 0 or 1; weight 100 to 700; optical size 20, 24, 40 or 48), tinted by its fill, 24 by 24 by default. The inspector has Glyph search, Style, Filled and Weight. The server pins the SVGs to a google/material-design-icons commit, caches them under `Storage:Icons` and serves them at `/api/icons`; the renderer rasterizes them with NanoSVG into the glyph atlas at the zoom bucket. A glyph the source lacks draws a missing-glyph box. Icons need snapshot 27 and are written only when a document uses some.

## Variant rules

A rule with an empty cell is the base rule: it applies in every cell, and more specific
cells override it. Several base rules for one layer apply in declaration order. A rule
may set `visible`, which overrides the layer's boolean property for that cell; an
instance override of `visible` wins over both. A layer hidden by a rule is hidden like
any other layer for rendering, hit testing, auto layout and prototype playback. Base
rules and rules that set `visible` need snapshot 24.

## Component states

A main component can declare up to 32 states, each with a lowercase kebab-case name and a
kind: `interaction` (`hover`, `active`, `focus` or `focus-visible`), `logical` or
`custom`. State names are separate from axes and properties. A rule may list declared
states beside its cell and matches only when its cell matches and every listed state is
active on the instance. Rules without states apply first, as above. Rules with states
apply after them, ordered by cell size, then by number of states, then by declaration
order, so a state rule overrides a non-state rule for the same field. Text properties and
instance overrides still apply last.

States are never authored: while editing, no state is active and state rules do not
apply. A playback copy of the document can hold the active states of one instance
occurrence, which are never saved. A definition cannot drop or rename a state that a rule
still lists. Declaring states or listing them in a rule needs snapshot 29.

## Persistence and rendering

Snapshots remain versioned by the features they contain. Text starts at version 5;
independent metrics, wrapping, face selection and shared typography progressively
require versions 6–11. Lines use 12, straight unfilled paths 13, curved/filled paths
14, raster images 15, click prototypes 16, overlay/dissolve options 17 and directional
transitions/overlay placement 18, component part names 23, base variant rules or rules that set visibility 24, node notes 26, icons 27, gradient tokens 28 and component states or rule states 29. Earlier documents retain their minimum version. Downlevel
snapshots containing unsupported newer data are refused.

Prototype playback uses the same native renderer and does not produce document
operations or modify Undo/Redo. Referenced screen deletion, nesting and promotion
are refused until incoming links are removed. See [native click prototypes](prototyping.md)
for authoring, keyboard controls and persistence details.

Curves use normalized absolute controls in a shared control-hull box. Adaptive
flattening targets 0.25 world units with a six-level subdivision cap, so extreme
curves may exceed that target. Stroke and even-odd fill share one coverage primitive
to avoid opacity overlap. Path points use a separate GPU buffer. Exceptionally large
visible scenes can exceed GPU storage binding limits; streaming batches are pending.

Raster identity hashes little-endian width/height followed by original RGBA8 bytes.
Native validation checks the digest, dimensions and canonical base64. GPU uploads
are premultiplied for filtering, while persisted pixels remain unchanged. Component
copies share content identity and may override crop/fit independently of the source.

Fonts and third-party notices are in `visualcore/assets/fonts/`,
`visualcore/third_party/stb/`, `visualcore/third_party/harfbuzz/` and `visualcore/third_party/nanosvg/`. No externally
loaded fonts or image URLs are needed.

Prototyping also supports named root-frame flow entries (snapshot 22), visual
hotspot-to-screen connection authoring with a keyboard alternative, focus-aware
player controls and full-document `.canvas.json` export/reopen in Assets. The file
preserves all authored screens, components, tokens and embedded images. Reopening
uses the configured server's native validator to create a separate document;
keep that server local for private local delivery. Hosted sharing and partial-tree Smart Animate matching remain pending.

## PNG export

The Export section of the inspector saves each selected node as `<name>@<scale>x.png` at 1x, 2x or 3x.
The node and its descendants are rendered offscreen by the canvas renderer, in the active token mode and
with the current font availability, cropped to the node's bounds and clipped by its ancestors. Selection,
handles, frame labels, grid and prototype connections are not part of the image, and unpainted areas are
transparent. Targets larger than 4096 px per side render in tiles. Images beyond 16384 px per side or
64 million pixels reduce the scale, and the file name and panel report the scale used.
