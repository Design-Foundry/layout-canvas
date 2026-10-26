# Native click prototypes

Use root **Frame** nodes as screens. In the inspector, switch from **Design** to
**Prototype**, select a layer and give it one **On click** action:

- **Navigate to screen** selects a named root frame in this document.
- **Back** returns through the preview's navigation history.
- **Open overlay** opens a root frame over the base screen, at the same camera
  scale. Center is the default; choose one of nine anchors or Manual X/Y from the
  base screen's top-left, in design units. The background is dimmed and cannot
  receive clicks.
- **Close overlay** closes the top overlay without navigating the base screen.
- **None** removes the action, including an inherited copied-layer action.

The same panel marks a root frame as a **Flow starting screen**. Multiple starting
screens are allowed; the toolbar chooses the entry to play. A selected navigation
action displays its connection on the editing canvas. This increment authors
connections through the inspector, without draggable connection endpoints.

## Playing a flow

**Play prototype** presents the chosen screen with the existing native canvas
renderer. The editor chrome and selection handles are hidden. **Back**, **Restart**
and **Exit** control the preview; Escape exits and Alt+Left goes back. Preview
navigation does not create document operations, change Undo/Redo or save the
current playback screen. Exiting restores the editing camera and selection.

Navigate supports **Instant**, **Dissolve**, **Slide** and **Push**; Open overlay
supports Instant, Dissolve and Slide. Slide/Push have left/right/up/down directions.
All animations take 1–2000 ms (default 300), with input blocked until completion.
Slide moves the incoming screen over a stationary outgoing base; Push moves both.
Direction names describe travel: Slide left enters from the right. Movement is
linear and deterministic. Modal content, scrims and hit testing stay clipped to
the base screen viewport, including manually positioned oversized modals.
Manual X/Y may range from -100000 to 100000; offscreen portions remain unclickable.

Dissolve paints each complete scene into an offscreen target, then mixes the
premultiplied scene colors once. Transition opacity therefore preserves overlapping
paints, glyphs and images; opaque equal-size screens remain opaque throughout.
This is scene composition, not matching-object interpolation. The outgoing screen
keeps its old camera scale and viewport when the destination has different dimensions.
Slide/Push use composed Dissolve when those rendered viewport shapes do not match,
so uncovered portions of the old screen do not disappear abruptly. The saved action
and duration remain unchanged. Navigating from an overlay drops the modal stack
and transitions the base screens; navigating to its existing base simply closes all
overlays immediately.

Playback follows the browser's `prefers-reduced-motion` setting. With reduced motion,
navigation and overlays appear instantly; changing the preference mid-transition
settles at the destination. Resizing the player or changing a screen's root geometry
through synchronization also finishes the transition there. These actions never
write document operations or change authored animation settings. Back, Restart and
Exit cancel the active transition and perform their normal explicit actions.

The compositor uses two viewport-size targets, reused during playback and released
on exit/detach. Above 16,777,216 render pixels it finishes the transition instantly
instead of allocating more than 128 MiB of scene targets. Document opacity and paint
semantics outside prototype scene mixing are unchanged.

The modal stack holds up to eight distinct screens. Reopening a screen already in
the stack or opening the base as its own overlay is ignored. **Dismiss when clicking
outside** is enabled by default and can be disabled per action. An outside click
closes only the top modal, without activating the underlying screen. Back closes
the top overlay before consulting base-screen history. Restart clears both stacks
and any transition. Close overlay is also available in the presentation controls.
Closing overlays is instantaneous. Hidden/deleted/invalid modal screens are safely
removed when remote document changes arrive.

The screen fits the available canvas and is clipped to its frame even when its
design-time clip setting is off. Hidden layers are not clickable; design locks do
not disable visible prototype hotspots. Clicking a text/icon child can activate an
ancestor's action. A deeper action takes precedence. Dragging, leaving the canvas
or canceling a press does not trigger navigation. Other overlapping editing frames
cannot intercept preview clicks. The history keeps at most 128 navigation hops.

Preview blocks authoring commands. Incoming document changes still apply through
normal synchronization; a screen that becomes invalid stops playback safely.
Frames must be visible and have positive computed dimensions to play. Component
masters, instances and nested frames are not screen destinations in this slice,
although their layers can provide or override hotspots inside a root frame.

## Persistence and references

Prototype metadata uses native snapshot **16**. A node may store
`interaction:{trigger:"click",action:"navigate",target:"frame-uuid"}` or
`interaction:{trigger:"click",action:"back"}`. An explicit patch
`interaction:null` clears it. Root frame `prototypeStart` flags are independent.
Earlier documents retain their earlier minimum snapshot versions.

Overlays and dissolve options require snapshot **17**. Actions use `open-overlay`
with a root-frame `target`, or `close-overlay` with no target. Navigate/Open overlay
may add `transition:"dissolve"` and integer `durationMs`; Instant omits these fields.
Only Open overlay accepts `dismissOutside:false` (true is the default). Unknown
options, invalid combinations, unsupported durations and downlevel snapshots are
refused by the native model, including independent component overrides.

Slide/Push and non-default overlay placements require snapshot **18**. Transition
names are `slide-left/right/up/down` or `push-left/right/up/down`. Push is only
valid for Navigate. Overlay `placement` accepts `center`, `top-left/center/right`,
`center-left/right`, `bottom-left/center/right` and `manual`; the slash groups here
denote individual names such as `top-center`. Manual placement requires exactly
`offset:{x:80,y:70}`; other placements refuse `offset`. Center defaults are omitted.
Durations are accepted only with non-instant transitions. New source and independent
override metadata refuse downlevel snapshots; earlier documents keep versions 16/17.

The model validates target identity and type, including forward and cyclic links
when loading a snapshot. Deleting, nesting or promoting a referenced destination
is refused until its incoming links are removed. Variant rules cannot author
prototype behavior. Copied-layer interactions use the normal independent override
mechanism, including inherited actions and explicit clears.

These links belong to a full document. Portable Canvas component files refuse
prototype metadata with an explanation instead of discarding links to screens
outside the component. Native document storage/reload remains self-contained.

## Scrollable screens and overlays

Select an authored root Frame, open Prototype, and choose **Overflow scrolling**:
No scrolling, Vertical, Horizontal or Both axes. Place content beyond the frame's
right/bottom edges. The frame size remains the viewport in Play; its background
and border stay fixed while descendants scroll and clip within it.

Use the wheel or trackpad; Shift-wheel scrolls horizontally. Single-touch drag
also scrolls, while a tap keeps hotspot activation. Dragging cancels the click.
Only the top modal receives scrolling, and its scrim blocks background scrolling.
Scrolling is blocked during transitions. Opening a modal or navigating to another
screen starts at zero; Back restores the previous screen's position. Restart and
starting a new Play session reset it. Resize preserves clamped scroll; incoming
document edits clamp offsets and cancel gestures/animations.

The native field is `prototypeOverflow:"vertical"|"horizontal"|"both"`, requiring
snapshot **19**. `"none"` clears it; default node metadata is omitted on save.
It is accepted only on authored root frames, never component overrides or rules.
Clear scrolling before nesting the frame or converting it into a component.
Portable component files refuse the metadata, including explicit `"none"`.
Scroll offsets stay in the player and produce no operations or undo entries.

Ranges use visible descendant layout boxes and honor nested clipping. Negative
content coordinates and paint bleed (such as shadows) do not extend the ranges.
Nested scroll containers, fixed/sticky layers, momentum and pinch zoom are not
part of this increment. Touch logic has native regression coverage; physical
touch-device behavior has not been verified.

## Multiple interactions and triggers

The Prototype inspector can add, select, remove and reorder up to 16 interactions
on a source layer or an independent copied-layer override. The selected row keeps
its own action, destination, transition and modal options. Triggers are On click,
On hover, On press and After delay. Delay requires an integer from 1 to 60000 ms.

The deepest ancestor with the current trigger wins; its first matching row runs.
Later rows with the same trigger are shadowed, including duplicate delays. Rows
are priority alternatives, not a chain of actions. Hover runs on pointer entry,
excluding touch and dragging. Primary press runs on down and consumes release click.
Delay runs once per node per active screen/modal visit, starting after transitions
settle. Hidden, clipped or offscreen nodes wait until they are visible. Timers on
underlying screens do not run beneath a modal. At most one due automatic action
runs per frame, in visible paint order, so cyclic flows cannot recurse in one frame.

Navigation, Back and closing a modal start a new delay epoch on the visible screen.
Viewport resizing restarts a complete delay epoch after the new layout, including
the initial expansion into Play. Remote edits cancel scheduled delays until an
explicit navigation or Restart; Exit cancels all trigger state. A context change
also cancels captured mouse/touch gestures. Playback adds no document operations.

New lists use native `interactions:[{trigger:"delay",delayMs:1500,action:"navigate",
target:"frame-uuid"}]` and require snapshot **20**. Setting a list atomically clears
legacy `interaction`, and setting legacy metadata clears the list. An explicit
empty list masks inherited behavior. Undo/Redo reverses that migration. Existing
click-only documents retain their old format/version until a list is authored.
Variants cannot store lists; raw instances use the normal override path. All
targets, including shadowed rows, remain protected references. Portable component
files refuse lists even when empty, instead of dropping document-local behavior.

## Interactive component states and Smart Animate

Give a reusable component a variant axis, then select a source or copied hotspot
and choose **Change component state** in Prototype. The inspector offers the
nearest component's declared axes and choices. During Play the action changes
the nearest authored instance independently; the component master and other
instances keep their authored values. A state action can use any existing trigger.

The native action is `{trigger:"click",action:"change-state",values:{state:"active"}}`.
It has no screen target and requires snapshot **21**. A values map contains 1–8
nonempty string pairs. Syntax is bounded on storage; Play also checks every axis
and choice against the actual component definition. Invalid choices, actions
outside an authored screen instance and derived nested instances do nothing.
Existing text, boolean, swap and independent copied-layer overrides are preserved;
the action itself changes only declared variant-axis choices.

Play uses a detached document with independent layout and expansion caches. States
persist across navigation, Back and modal use, and reset on Restart, Exit or incoming
authored edits. They never alter source values, Undo history or outgoing document
operations. Authoring the action remains an ordinary persisted, undoable edit.

Choose Instant, Dissolve or Smart animate for a state action. Smart animate also
appears on Navigate; overlays keep their previous transition options. Compatible
expanded node and paint identities interpolate geometry, clip rectangles, corner
radii, path points, color and opacity linearly. Image pixels/crops and glyph UVs
stay fixed. Changed text/glyphs, changed image assets/crops, reordered/missing paints
or incompatible path topology use a complete-scene dissolve instead. Distinct
screen roots have distinct identities, so cross-screen Navigate currently takes
that fallback. This is a conservative matching policy, not name-based layer matching.

Reduced motion skips animation while applying the destination state. Input remains
blocked until a transition settles. Raw nested copied-instance states, conditional
prototype rules and portable interactive-component packaging need further contracts;
portable component files continue refusing prototype metadata rather than dropping it.

Variables, conditional actions, external URLs and hosted sharing are not implemented.
See the [staged implementation plan](prototyping-plan.md) for sequencing and acceptance.
