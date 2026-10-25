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
