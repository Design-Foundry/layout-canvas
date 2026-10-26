# Prototyping validation

## Stage 7: nested component states and cross-screen Smart Animate

The public checkout passed **646 automated checks**: 370 native, 156 Angular,
50 Node integration, 38 server and 32 live localhost HTTP checks. Native, WASM,
bridge TypeScript, client production and server builds passed. Native/server DLL
SHA256 matches:
`6D8908B9686248605222439A7AA82A343E95629F1ECD987A5FAEC476AC99972D`.

Native regressions cover sibling/outer occurrence isolation, three nesting levels,
nearest-instance resolution, preserved overrides, invalid values, swapped effective
component identities, immutable outgoing documents, navigation, Restart and remote
invalidation. Cross-screen regressions cover moved geometry, expanded occurrences,
ambiguous/empty names, hidden/missing/renamed/type-mismatched hierarchies, paint
order and incompatible image assets/UVs, glyphs and paths. The inspector test checks
axes from the nearest effective nested component. An HTTP fixture assertion was
scoped to its unique document name so concurrent test files cannot produce false
partial-import failures.

Real browser QA activated one of four nested copies; only that copy grew and
changed color. Restart restored all four. A second document navigated between
same-hierarchy screens with an embedded RGBA image: an active-transition capture
showed one moving/resizing image, followed by the final destination geometry.
Both source documents retained operation head 1 after playback. Browser warnings
and WebGPU errors were empty. Proof screenshots and native operation evidence stay
local and are excluded from publication.

No persistence version changed. Cross-screen matching requires identical complete
expanded name/type paths with unique nonempty sibling names; incompatible scenes
still dissolve. Partial-tree matching, paint reordering, variables, conditions and
hosted sharing are not part of this increment. Earlier stage limitations below
record the behavior at those stages; Stage 7 supersedes their nested/matching gaps.

## Stage 6: named flows and local full-document delivery

Validated the public branch with **634 automated checks**: 359 native, 155 Angular,
50 Node file/integration, 38 server and 32 live localhost HTTP checks. Native,
WASM release, bridge TypeScript, Angular production and server builds passed.
Native/server DLL SHA256 matches:
`2615CB925E60058FF5CACB3040890218184D38B0FEC2FEE899B68927F7DB1C01`.

Native tests cover snapshot 22 names, explicit empty-name downgrade refusal,
root-only placement, reversible clearing, text bounds and override/rule refusal.
Portable component files accept plain version 22 while refusing flow metadata.
Client tests cover named entry authoring, preserved trigger lists, pointer drop,
selection cancellation, keyboard connection authoring, player controls and local
file errors. HTTP tests reopen a complete file twice without changing any native
content, including flow names, state actions, destinations and verified image
pixels; corrupt assets and missing targets fail without creating partial documents.

Real browser QA on localhost verified an edited flow label in the picker, pointer
drag from a hotspot to a second screen, Undo/Redo, persisted reconstruction after
reload and actual Play navigation through that link. Play moved focus into controls
and Escape returned focus to Play. Browser warnings/errors were empty. A real
exported `.canvas.json` reopened through the file chooser into a separate document;
the stored native snapshot exactly matched the exported snapshot, preserving its
image and named flow. The source operation head remained unchanged by Play/export/
reopen, while the imported copy began at sequence 1. Local proof files/screenshots
are excluded from Git and no private integration was published.

The 2 MiB snapshot limit includes inline images. This deliverable needs a local
Canvas server to reopen and is not standalone HTML or hosted sharing. Derived
nested component states and cross-screen Smart Animate matching remain pending.


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

## Stage 5: isolated component states and conservative Smart Animate

The public branch passed **602 automated checks**: 355 native, 138 Angular,
41 Node interchange, 38 server and 30 live localhost HTTP checks. Native, WASM
release, bridge TypeScript, production client and server builds pass. The final
native/server DLL SHA256 matches:
`24C55747D01CE5A6B8732B0D1DA473959B281AD0217B53674086E4C45C796420`.

Native tests verify detached cache ownership, independent instance state, unchanged
authoring snapshots/outbox/Undo, retention of authored properties and overrides,
state persistence through navigation and resets on Restart/remote edits. They also
cover invalid context/axis/choice refusal, derived nested-instance refusal, reduced
motion, immutable outgoing state and midpoint paint interpolation. Draw tests cover
stable identity, geometry/color/path interpolation, UV preservation and conservative
fallback. Model/HTTP tests exercise strict state syntax, override persistence,
transition combinations, snapshot 21 and downgrade refusal. Inspector tests cover
declared-axis discovery/selection, imported unavailable choices and list preservation.

Browser QA used two instances of one native component containing an immutable RGBA
image. Expanding one instance changed its image geometry, background and radii while
the other remained unchanged. An active-transition capture had `aria-busy=true`;
the final capture had `false`. Changing text used a whole-scene dissolve, and Restart
restored both instances. The imported document stayed at sequence 1 with empty
authored instance values after playback. Inspector state selection, Smart Animate,
Undo/Redo, confirmed operation-log persistence and reload were then verified.
No WebGPU validation messages were observed. Physical touch hardware remains
unverified. Cross-screen identity matching and derived nested instance states are
explicit limitations, not verified Figma parity.

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
