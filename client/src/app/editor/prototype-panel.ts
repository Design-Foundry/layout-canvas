import { Component, computed, effect, input, signal } from '@angular/core';
import type {
  NodePatch,
  NodeProps,
  PrototypeScreen,
  PrototypeInteraction,
  PrototypeTransition,
  PrototypeOverlayTransition,
  PrototypePlacement,
  PrototypeOverflow,
} from '@canvas/bridge';

const OVERLAY_TRANSITIONS: readonly PrototypeOverlayTransition[] = [
  'dissolve',
  'slide-left',
  'slide-right',
  'slide-up',
  'slide-down',
];
const NAVIGATE_TRANSITIONS: readonly PrototypeTransition[] = [
  ...OVERLAY_TRANSITIONS,
  'push-left',
  'push-right',
  'push-up',
  'push-down',
];
const PLACEMENTS: readonly PrototypePlacement[] = [
  'center',
  'top-left',
  'top-center',
  'top-right',
  'center-left',
  'center-right',
  'bottom-left',
  'bottom-center',
  'bottom-right',
  'manual',
];

@Component({
  selector: 'app-prototype-panel',
  template: `<section aria-label="Prototype interactions">
    <h3>On click</h3>
    <label
      >Action
      <select
        aria-label="Prototype action"
        [value]="value().interaction?.action ?? 'none'"
        [disabled]="!!node().swapBound"
        (change)="action($event)"
      >
        <option value="none">None</option>
        <option value="navigate" [disabled]="!screens().length">Navigate to screen</option>
        <option value="back">Back</option>
        <option value="open-overlay" [disabled]="!screens().length">Open overlay</option>
        <option value="close-overlay">Close overlay</option>
      </select></label
    >
    @if (linked()) {
      <label
        >Destination
        <select
          aria-label="Prototype destination"
          [value]="destination()"
          [disabled]="!!node().swapBound"
          (change)="target($event)"
        >
          @for (screen of screens(); track screen.id) {
            <option [value]="screen.id" [selected]="screen.id === destination()">
              {{ screen.name || 'Untitled frame' }}
            </option>
          }
        </select></label
      >
      <label
        >Transition
        <select
          aria-label="Prototype transition"
          [value]="linked()?.transition ?? 'instant'"
          [disabled]="!!node().swapBound"
          (change)="transition($event)"
        >
          <option value="instant" [selected]="!linked()?.transition">Instant</option>
          @for (choice of transitions(); track choice) {
            <option [value]="choice" [selected]="linked()?.transition === choice">
              {{ transitionLabel(choice) }}
            </option>
          }
        </select></label
      >
      @if (linked()?.transition) {
        <label
          >Duration (ms)
          <input
            type="number"
            min="1"
            max="2000"
            step="1"
            aria-label="Prototype duration"
            [value]="linked()?.durationMs ?? 300"
            [disabled]="!!node().swapBound"
            (change)="duration($event)"
          />
        </label>
      }
      @if (linked()?.action === 'open-overlay') {
        <label
          >Placement
          <select
            aria-label="Overlay placement"
            [value]="placementValue()"
            [disabled]="!!node().swapBound"
            (change)="placement($event)"
          >
            @for (choice of placements; track choice) {
              <option [value]="choice" [selected]="placementValue() === choice">
                {{ placementLabel(choice) }}
              </option>
            }
          </select></label
        >
        @if (placementValue() === 'manual') {
          <label
            >X (design units)<input
              type="number"
              step="any"
              min="-100000"
              max="100000"
              aria-label="Overlay offset X"
              [value]="overlayOffset().x"
              [disabled]="!!node().swapBound"
              (change)="offset($event, 'x')"
          /></label>
          <label
            >Y (design units)<input
              type="number"
              step="any"
              min="-100000"
              max="100000"
              aria-label="Overlay offset Y"
              [value]="overlayOffset().y"
              [disabled]="!!node().swapBound"
              (change)="offset($event, 'y')"
          /></label>
          <p>Position from the base screen's top-left corner.</p>
        }
        <label
          ><input
            type="checkbox"
            aria-label="Dismiss overlay outside"
            [checked]="outsideDismiss()"
            [disabled]="!!node().swapBound"
            (change)="dismiss($event)"
          />
          Dismiss when clicking outside</label
        >
      }
    }
    @if (rootFrame()) {
      <label
        >Overflow scrolling
        <select
          aria-label="Prototype overflow"
          [value]="value().prototypeOverflow ?? 'none'"
          (change)="overflow($event)"
        >
          <option
            value="none"
            [selected]="!value().prototypeOverflow || value().prototypeOverflow === 'none'"
          >
            No scrolling
          </option>
          <option value="vertical" [selected]="value().prototypeOverflow === 'vertical'">
            Vertical
          </option>
          <option value="horizontal" [selected]="value().prototypeOverflow === 'horizontal'">
            Horizontal
          </option>
          <option value="both" [selected]="value().prototypeOverflow === 'both'">Both axes</option>
        </select></label
      >
      <p>
        Scroll content beyond this frame's viewport during Play. Applies when used as a screen or
        overlay.
      </p>
      <label
        ><input
          type="checkbox"
          aria-label="Flow starting screen"
          [checked]="!!value().prototypeStart"
          (change)="start($event)"
        />
        Flow starting screen</label
      >
    }
    <p>
      Click interactions navigate between root frames, position overlays, close them or go back.
      Play the flow from the toolbar.
    </p>
    @if (!screens().length) {
      <p>Create a visible root frame with the Frame tool (F) to add a destination.</p>
    }
    @if (node().inInstance) {
      <p>Interaction changes on this copied layer are instance overrides.</p>
    }
    @if (error()) {
      <p role="alert">{{ error() }}</p>
    }
  </section>`,
  styles: `
    section {
      display: grid;
      gap: 12px;
      padding: 12px;
    }
    h3,
    p {
      margin: 0;
      font: inherit;
    }
    p {
      color: var(--color--semantic--text-muted);
      font-size: 12px;
    }
    label {
      display: grid;
      gap: 6px;
    }
    select {
      background: var(--color--semantic--surface-default);
      color: inherit;
      border: 1px solid var(--color--semantic--border-subtle);
      padding: 6px;
    }
    [role='alert'] {
      color: var(--color--semantic--error);
    }
  `,
})
export class PrototypePanel {
  readonly node = input.required<NodeProps>();
  readonly screens = input<readonly PrototypeScreen[]>([]);
  readonly save = input.required<(id: string, patch: NodePatch) => boolean>();
  private readonly pending = signal<NodePatch | null>(null);
  protected readonly error = signal('');
  protected readonly value = computed(() => ({ ...this.node(), ...this.pending() }));
  protected readonly linked = computed(() => {
    const interaction = this.value().interaction;
    return interaction?.action === 'navigate' || interaction?.action === 'open-overlay'
      ? interaction
      : null;
  });
  protected readonly destination = computed(() => {
    return this.linked()?.target ?? '';
  });
  protected readonly outsideDismiss = computed(() => {
    const interaction = this.linked();
    return interaction?.action !== 'open-overlay' || interaction.dismissOutside !== false;
  });
  protected readonly transitions = computed<readonly PrototypeTransition[]>(() =>
    this.linked()?.action === 'open-overlay' ? OVERLAY_TRANSITIONS : NAVIGATE_TRANSITIONS,
  );
  protected readonly placements = PLACEMENTS;
  protected readonly placementValue = computed(() => {
    const interaction = this.linked();
    return interaction?.action === 'open-overlay' ? (interaction.placement ?? 'center') : 'center';
  });
  protected readonly overlayOffset = computed(() => {
    const interaction = this.linked();
    return interaction?.action === 'open-overlay'
      ? (interaction.offset ?? { x: 0, y: 0 })
      : { x: 0, y: 0 };
  });
  protected transitionLabel(choice: PrototypeTransition): string {
    return choice.charAt(0).toUpperCase() + choice.slice(1).replace('-', ' ');
  }
  protected placementLabel(choice: PrototypePlacement): string {
    return choice.charAt(0).toUpperCase() + choice.slice(1).replace('-', ' ');
  }
  protected readonly rootFrame = computed(
    () => this.node().type === 'frame' && !this.node().parent && !this.node().inInstance,
  );
  constructor() {
    effect(() => {
      this.node();
      this.pending.set(null);
      this.error.set('');
    });
  }
  protected action(event: Event): void {
    const select = event.target as HTMLSelectElement;
    if (this.node().swapBound) {
      select.value = this.value().interaction?.action ?? 'none';
      return;
    }
    const value = select.value;
    let interaction: PrototypeInteraction | null | undefined;
    if (value === 'none') interaction = null;
    else if (value === 'back' || value === 'close-overlay')
      interaction = { trigger: 'click', action: value };
    else if ((value === 'navigate' || value === 'open-overlay') && this.screens().length) {
      const previous = this.linked(),
        target = previous?.target ?? this.screens()[0].id;
      if (value === 'open-overlay') {
        const transition =
          previous?.transition &&
          OVERLAY_TRANSITIONS.includes(previous.transition as PrototypeOverlayTransition)
            ? (previous.transition as PrototypeOverlayTransition)
            : undefined;
        interaction = {
          trigger: 'click',
          action: value,
          target,
          ...(previous?.action === 'open-overlay' ? previous : {}),
          ...(transition ? { transition, durationMs: previous?.durationMs ?? 300 } : {}),
        };
      } else
        interaction = {
          trigger: 'click',
          action: value,
          target,
          ...(previous?.transition
            ? { transition: previous.transition, durationMs: previous.durationMs ?? 300 }
            : {}),
        };
    }
    if (interaction === undefined || !this.change({ interaction }))
      select.value = this.value().interaction?.action ?? 'none';
  }
  protected target(event: Event): void {
    const select = event.target as HTMLSelectElement;
    if (
      this.node().swapBound ||
      !this.linked() ||
      !this.screens().some((screen) => screen.id === select.value) ||
      !this.change({ interaction: { ...this.linked()!, target: select.value } })
    )
      select.value = this.destination();
  }
  protected transition(event: Event): void {
    const control = event.target as HTMLSelectElement,
      interaction = this.linked();
    if (
      !interaction ||
      this.node().swapBound ||
      (control.value !== 'instant' &&
        !this.transitions().includes(control.value as PrototypeTransition))
    ) {
      control.value = interaction?.transition ?? 'instant';
      return;
    }
    const { transition: _transition, durationMs: _duration, ...base } = interaction;
    const changed =
      control.value === 'instant'
        ? base
        : {
            ...base,
            transition: control.value as PrototypeTransition,
            durationMs: interaction.durationMs ?? 300,
          };
    if (
      !this.change({
        interaction: changed as PrototypeInteraction,
      })
    )
      control.value = interaction.transition ?? 'instant';
  }
  protected duration(event: Event): void {
    const control = event.target as HTMLInputElement,
      interaction = this.linked(),
      durationMs = Number(control.value);
    if (
      !interaction ||
      !interaction.transition ||
      this.node().swapBound ||
      !Number.isInteger(durationMs) ||
      durationMs < 1 ||
      durationMs > 2000 ||
      !this.change({ interaction: { ...interaction, durationMs } })
    )
      control.value = String(interaction?.durationMs ?? 300);
  }
  protected placement(event: Event): void {
    const control = event.target as HTMLSelectElement,
      interaction = this.linked(),
      placement = control.value as PrototypePlacement;
    if (
      interaction?.action !== 'open-overlay' ||
      this.node().swapBound ||
      !PLACEMENTS.includes(placement)
    ) {
      control.value = this.placementValue();
      return;
    }
    const { placement: _placement, offset: _offset, ...base } = interaction;
    const changed =
      placement === 'manual'
        ? { ...base, placement, offset: interaction.offset ?? { x: 0, y: 0 } }
        : placement === 'center'
          ? base
          : { ...base, placement };
    if (!this.change({ interaction: changed })) control.value = this.placementValue();
  }
  protected offset(event: Event, axis: 'x' | 'y'): void {
    const control = event.target as HTMLInputElement,
      interaction = this.linked(),
      value = Number(control.value);
    if (
      interaction?.action !== 'open-overlay' ||
      interaction.placement !== 'manual' ||
      this.node().swapBound ||
      !control.value.trim() ||
      !Number.isFinite(value) ||
      Math.abs(value) > 100000 ||
      !this.change({
        interaction: { ...interaction, offset: { ...this.overlayOffset(), [axis]: value } },
      })
    )
      control.value = String(this.overlayOffset()[axis]);
  }
  protected dismiss(event: Event): void {
    const control = event.target as HTMLInputElement,
      interaction = this.linked();
    if (interaction?.action !== 'open-overlay' || this.node().swapBound) {
      control.checked =
        interaction?.action === 'open-overlay' ? interaction.dismissOutside !== false : true;
      return;
    }
    const { dismissOutside: _outside, ...base } = interaction;
    if (!this.change({ interaction: control.checked ? base : { ...base, dismissOutside: false } }))
      control.checked = interaction.dismissOutside !== false;
  }
  protected start(event: Event): void {
    const checkbox = event.target as HTMLInputElement;
    if (!this.rootFrame() || !this.change({ prototypeStart: checkbox.checked }))
      checkbox.checked = !!this.value().prototypeStart;
  }
  protected overflow(event: Event): void {
    const control = event.target as HTMLSelectElement;
    const value = control.value as PrototypeOverflow;
    if (
      !this.rootFrame() ||
      !['none', 'vertical', 'horizontal', 'both'].includes(value) ||
      !this.change({ prototypeOverflow: value })
    )
      control.value = this.value().prototypeOverflow ?? 'none';
  }
  private change(patch: NodePatch): boolean {
    if (this.save()(this.node().id, patch)) {
      this.pending.update((old) => ({ ...old, ...patch }));
      this.error.set('');
      return true;
    }
    this.error.set('The prototype edit was refused. The saved prototype settings were kept.');
    return false;
  }
}
