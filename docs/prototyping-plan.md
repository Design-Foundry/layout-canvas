# Prototyping implementation plan

Working branch: `codex/prototyping-roadmap`. General editor functionality only.

## Validated baseline

Native click Navigate/Back, root-frame entry screens, isolated playback, centered
Open/Close overlay, outside dismissal, independent component hotspot overrides,
basic per-paint dissolve and snapshots 16/17 are implemented. Upstream main at
`9f96ccc` adds structured diagnostics, live MCP edits and filtered glyph rendering.
The native baseline is covered by model, runtime and browser regressions.

## Sequence and acceptance

| Stage | Deliverable | Acceptance | Status |
| --- | --- | --- | --- |
| 1 | Directional Slide/Push and positioned overlays | Strict native schema and downgrade refusal; deterministic native layers and matching modal hit testing; inspector authoring, Undo/Redo and reload; real local playback with unchanged document operations | Complete |
| 2 | Offscreen composition and transition polish | Correct group alpha for overlapping paints/images/text; preserved outgoing viewport across unequal screens; reduced-motion preference and consistent interrupted transition policy | Complete |
| 3 | Scrollable screen and overlay viewports | Explicit overflow contract, wheel/touch scroll with clipping and consistent picking; fixed/sticky elements only after native layout semantics exist; reset and remote invalidation behavior tested | Planned |
| 4 | Multiple interactions and triggers | Versioned ordered interaction list, click/hover/press/delay semantics, cancellation and timer lifecycle, source/instance overrides and backward migration | Planned |
| 5 | Interactive components and Smart Animate | Variant state transitions without authoring mutations; stable matching identities, interpolation policy per property, conservative fallback for incompatible nodes; real component and image demos | Planned |
| 6 | Flows and delivery | Named flow entries, visual connector authoring, accessible player controls, full-document local export/reopen; sharing only after explicit hosting/auth scope | Planned |

Variables, expressions, conditions and external URLs need separate contracts.
Portable component files refuse document-local screen references rather than dropping them.

## Stage 1 contract

Add native Slide/Push in four directions, with durations 1–2000 ms. Slide moves the
incoming screen over a stationary outgoing base; Push moves both. The native
presentation renderer and hit testing consume the same ephemeral layer geometry.
Input remains blocked until the animation finishes; Back/Restart/Exit cancel it.

Open overlay gains nine anchor placements plus manual x/y relative to the base
screen's top-left, in design units. Modal placement must drive both drawing and
hit testing; outside dismissal cannot activate the background. Existing centered
overlays and snapshot 17 retain their original semantics. New authored settings
require snapshot 18, including copied-layer overrides.

## Commit and verification policy

Commit the plan first, then native schema/runtime with regressions, inspector and
bridge, and usage/evidence. Keep each commit reviewable. Validate native tests,
Angular tests, Node integration, server tests and live localhost HTTP where their
contracts change. Build WASM and production client, verify native/server DLL parity,
then inspect actual playback and persisted native reconstruction. Update stage status from observed results.

## Execution record

Stage 1 includes native schema, rendering and hit-test regressions, inspector
authoring tests, portable-file refusal checks and HTTP persistence tests. Real
browser QA verified Undo/Redo, reload, moving screens, positioned hotspot closure,
input blocking, outside dismissal and Restart cancellation without playback writes.

Stage 2 paints full native scenes into two offscreen targets and mixes their
premultiplied colors once. Outgoing screens keep their previous camera transform.
Directional navigation falls back to composed dissolve when viewport shapes do
not match, without changing authored metadata. Reduced-motion changes, viewport
resizes and remote screen geometry changes finish the active transition at its
destination; Back/Restart/Exit retain their explicit navigation behavior.

Stage 2 validation covers 526 automated checks plus active-transition browser
capture and a pixel comparison of identical scenes containing translucent overlaps,
glyphs and an RGBA image. See [validation](prototyping-validation.md).

Next implementation stage is scrollable screen and overlay viewports. Stages
3–6 remain planned; these increments are not full Figma prototyping parity.
