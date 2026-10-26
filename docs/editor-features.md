# Editor capabilities and limits

The editor stores these features in native document operations and snapshots;
canvas rendering, Undo/Redo, component inheritance and portable files use the same
contracts. This is an editor foundation increment, not a claim of full Figma parity.

| Area | Available | Current limits |
| --- | --- | --- |
| Components | Main components, linked instances, boolean/text/swap properties, variant axes, per-field rule editing, independent overrides, guarded main switching, reset/detach | Some edits that would invalidate existing override addresses are refused |
| Text | On-canvas and inspector editing, multiline/wrapping, Hug sizing, metric overrides and declared string properties | A bounded single run of supported Latin glyphs; no rich runs, complex shaping or glyph-level canvas caret |
| Typography | Bundled Inter Regular/Bold/Oblique/Bold Oblique, actual font metrics/kerning, local shared typography tokens, per-field instance inheritance | Fixed trusted faces, literal shared style metrics; no font uploads, fallback or aliases/modes for composite typography |
| Vectors | Rectangles, ellipses, editable round-cap lines, cubic paths, anchor/control editing, even-odd fills, color tokens and opacity | Up to 64 anchors; one contour and common fill/stroke paint; no boolean operations or cap/join choices |
| Drawing | Pen click/drag, Pencil, five-sided Polygon, five-point Star, Shift constraints, Alt-centered shapes, gesture cancellation | Pencil reduces to 64 samples and does not retain pressure or smooth; polygons/stars become editable paths without parametric controls |
| Images | Local static PNG/JPEG import, native RGBA pixels, SHA-256 identity, fixed geometry, contain/cover/stretch, normalized crop, opacity and clipping | 4 MB input, 4096 px source side, explicit downsample to 512 px, eight identities and 1,400,000 encoded pixel characters per document; no original-resolution recovery, animation or effects |
| Exchange | Portable `canvas-component/1` files with native validation and dependent mains/tokens; atomic import into a new document | 2 MB files; malformed/unsupported data refuses instead of silent substitution |
| Prototyping | Root-frame screens, starting screen selection, Navigate/Back/Open/Close overlay, positioned modals, scene-composited dissolve/slide/push, preserved outgoing viewports, reduced motion, scrollable screen/modal viewports, inherited hotspot overrides, ordered click/hover/press/delay interactions, isolated component states, conservative Smart Animate and native playback | No cross-screen identity matching, derived nested instance states, fixed/sticky or nested scroll containers, variables, URLs or hosted sharing; directional transitions use dissolve across mismatched viewport shapes; portable files refuse document-local links |

## Persistence and rendering

Snapshots remain versioned by the features they contain. Text starts at version 5;
independent metrics, wrapping, face selection and shared typography progressively
require versions 6–11. Lines use 12, straight unfilled paths 13, curved/filled paths
14, raster images 15, click prototypes 16, overlay/dissolve options 17 and directional
transitions/overlay placement 18. Earlier documents retain their minimum version. Downlevel
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

Fonts and third-party notices are in `visualcore/assets/fonts/` and
`visualcore/third_party/stb/`. No externally loaded fonts or image URLs are needed.
