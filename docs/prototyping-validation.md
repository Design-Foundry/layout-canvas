# Prototyping validation

## Stage 2: complete scene composition and motion policy

Passed **526 automatic checks**: 310 native, 119 Angular, 35 Node integration,
38 server and 24 live localhost HTTP. Native, WASM release, bridge, production
client and server builds passed. New regressions cover complete from/to scenes,
nested-modal backgrounds, affine geometry/UV scaling, previous viewport preservation,
all directional fallbacks, preference changes and viewport/geometry interruptions.

Browser QA captured an active two-second dissolve between visually identical scenes
with an opaque base, two overlapping translucent rectangles, a semi-transparent
RGBA image and glyphs. `aria-busy` was true during capture. The content region differed
by at most 3 channel levels out of 255 between JPEG captures, within the declared
4-level capture tolerance, with no pixel beyond that tolerance. WebGPU validation
reported no errors. This checks scene-transition opacity; it does not claim changes
to ordinary document-group paint semantics.

Reduced motion is an ephemeral player preference, and playback policies do not
change snapshot version 18 or authored actions. Oversized render viewports settle
instantly rather than exceeding the two-target allocation budget. Scrolling, more
triggers and Smart Animate remain separate planned increments.

## Stage 4: ordered trigger lists

The public branch passed **570 automated checks**: 338 native, 127 Angular,
39 Node interchange, 38 server and 28 live localhost HTTP checks. Native, WASM
release, bridge TypeScript, production client and server builds pass. The final
native/server DLL SHA256 matches:
`C5691E6FCB86189457AB0E90B0B5254082C5C744D3D0B44A636A3E26105CB150`.

Model tests cover strict lists, delays, legacy/list migration, Undo/Redo, instance
overrides, empty masks, target safeguards, snapshot 20 and downgrade refusal.
Runtime regressions cover event-specific ancestor priority, duplicate shadowing,
press consumption, hover exclusions, one due timer per frame, transition pause,
modal suspension, scroll/visibility, remote cancellation and viewport delay epochs.
An automatic modal entry cancels a captured base touch. A regression reproduces
the initial stage expansion after an earlier frame, rather than assuming a fixed
viewport on Play entry.

Browser QA verified click navigation, press without a second click action, hover
overlay opening/closing and automatic delay navigation. Direct Play entry into
the timed screen also reached its destination after the initial viewport expanded.
Playback left the imported document at sequence 1. Inspector reorder, Undo, trigger
and delay editing were checked against persisted operations and reload. No WebGPU
validation messages were observed. Physical touch hardware remains unverified.

## Stage 3: scrollable viewports

The public Layout Canvas branch passed **545 automated checks**: 323 native,
122 Angular, 37 Node interchange, 38 server and 25 live localhost HTTP checks.
Native, WASM release, bridge TypeScript, production client and server builds pass.
Native/server `vc_native.dll` SHA256 matches:
`5320FCB5094A432F1E15F970FD3F67CC50D47025BE8F981FC783355F2DAE906B`.

Native regressions cover stationary root paint and clipping, transformed picking,
positive content bounds with hidden/expanded/clipped descendants, axis limits,
modal isolation, outgoing scene scroll, history restoration, reset, remote shrink,
tap versus touch drag/cancel and prevention of playback writes. HTTP tests verify
snapshot 19, persisted overflow operations/reset, refused overrides/misplacement,
invalid values and downgrade rejection.

Browser QA verified vertical screen and modal wheel scrolling, closing through a
scrolled hotspot, navigation from scrolled content, Back restoration and Restart.
The modal viewport and underlying screen stay fixed. Playback left the imported
document at operation sequence 1. Inspector editing, Undo/Redo and reload preserve
the authored overflow value. No WebGPU validation messages were observed.
Single-touch logic is tested natively; physical touch-device behavior remains
unverified. Fixed/sticky layers, nested scroll containers, momentum and pinch zoom
are outside this increment.

## Stage 1: native prototype authoring

This branch is based on main `9f96ccc` and includes native click navigation,
Back, screen entry points, modal Open/Close actions, outside dismissal, timed
Dissolve/Slide/Push, nine modal anchors, manual coordinates and snapshot 16–18
persistence. See [usage](prototyping.md) and the [implementation plan](prototyping-plan.md).

The exact branch checkout passed **517 automatic checks**:

- 303 native model/runtime/rendering checks.
- 117 Angular checks, including inspector state and operation-log synchronization.
- 35 Node component/file integration checks.
- 38 server checks, including MCP and native diagnostics.
- 24 live localhost HTTP checks, including each directional transition and manual placement.

Native, WASM release, bridge TypeScript, Angular production and server builds passed
with an isolated dependency installation using the frozen lockfile. Portable
component files refuse document-local prototype references instead of losing them.

An isolated browser preview loaded snapshot 18, entered native Play, opened the
manually positioned modal and closed it through presentation controls. Exit
restored the editor, and browser warning/error logs were empty.

The stage-1 baseline used per-paint dissolve. Stage 2 replaces that with complete
scene composition; root Frame destinations and linear directional motion remain
the bounded prototype contract.
