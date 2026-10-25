# Prototyping implementation plan

Repository: [Design-Foundry/layout-canvas](https://github.com/Design-Foundry/layout-canvas) (public).
Working branch: `codex/prototyping-roadmap`. General editor functionality only.

## Validated baseline

Native click Navigate/Back, root-frame entry screens, isolated playback, centered
Open/Close overlay, outside dismissal, independent component hotspot overrides,
basic per-paint dissolve and snapshots 16/17 are implemented. Upstream main at
`55bff8f` adds structured diagnostics, live MCP edits and filtered glyph rendering.
The branch also integrates public main `949e7f1`: theme-aware canvas/grid,
unfilled frame/component defaults, Apache 2.0 licensing and updated attribution.
The native baseline is covered by model, runtime and browser regressions.

## Sequence and acceptance

| Stage | Deliverable | Acceptance | Status |
| --- | --- | --- | --- |
| 1 | Directional Slide/Push and positioned overlays | Strict native schema and downgrade refusal; deterministic native layers and matching modal hit testing; inspector authoring, Undo/Redo and reload; real local playback with unchanged document operations | Complete |
| 2 | Offscreen composition and transition polish | Correct group alpha for overlapping paints/images/text; preserved outgoing viewport across unequal screens; reduced-motion preference and consistent interrupted transition policy | Complete |
| 3 | Scrollable screen and overlay viewports | Explicit overflow contract, wheel/touch scroll with clipping and consistent picking; fixed/sticky elements only after native layout semantics exist; reset and remote invalidation behavior tested | Complete |
| 4 | Multiple interactions and triggers | Versioned ordered interaction list, click/hover/press/delay semantics, cancellation and timer lifecycle, source/instance overrides and backward migration | In progress |
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

Stage 3 adds `prototypeOverflow: none | vertical | horizontal | both` on authored
root frames only, requiring snapshot 19 for active scrolling. The viewport is
the authored frame size; only descendants scroll. Positive content ranges use
visible layout boxes, including expanded component copies and descendant clips.
Paint shadows and negative-origin content do not extend those ranges.

Wheel/trackpad deltas and single-touch drag scroll the active screen or top modal.
Modal scrims block background scrolling. Navigation and opening an overlay start
at zero; Back restores the previous screen's scroll, while Restart/exit reset it.
Remote edits clamp active scroll and cancel pending gestures/animations. Resize
keeps the clamped position and cancels the gesture. Playback produces no document
operations, undo entries or serialized scroll offsets. Fixed/sticky positioning,
momentum, nested scroll containers and pinch zoom remain separate work.

Stages 4–6 remain planned; these increments are not full Figma prototyping parity.

## Stage 4 contract

Snapshot 20 introduces `interactions`, an ordered list of at most 16 entries using
the existing action options and `trigger: click | hover | press | delay`.
Only delay accepts (and requires) integer `delayMs` from 1 through 60000. An empty
list clears/masks authored behavior. Setting the list clears legacy `interaction`,
and setting the legacy field clears the list, with reversible Undo/Redo. Older
documents keep their click field and minimum versions until a list is authored.
Independent copied-layer overrides may replace/mask the list; variants may not.
All targets, including shadowed entries, retain native reference safeguards.

Pointer events resolve the deepest ancestor with that trigger, using its first
matching list entry. Duplicate triggers are priority alternatives, not action
chains; the first delay also shadows later delays on the same node. Hover fires
on pointer entry, not touch or dragging. Primary press consumes the release click.
Delay fires once per node per active screen/modal visit after the transition has
settled; hidden/clipped/offscreen nodes wait until visible. Only the top modal
has active timers. At most one due action runs per frame, in visible paint order,
so cyclic flows cannot recurse within a frame. Back/Close reveal a new timed visit.
Resize and remote edits cancel scheduled delays until explicit navigation/restart.
Exit cancels all trigger state. Timers, hover and presses never author document ops.

Stage 3 validation: 545 automated checks and real browser wheel/hotspot/history,
inspector Undo/Redo and reload checks passed. Builds and matching native/server
DLLs verified; see the validation record for touch-device limits.
