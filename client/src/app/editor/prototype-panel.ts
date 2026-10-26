import { Component, computed, effect, input, signal } from '@angular/core';
import type {
  NodePatch,
  NodeProps,
  PrototypeScreen,
  PrototypeInteraction,
  PrototypeTrigger,
  PrototypeTriggeredInteraction,
  PrototypeTransition,
  PrototypeOverlayTransition,
  PrototypePlacement,
  PrototypeOverflow,
  VariantAxis,
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
  'smart-animate',
];
const STATE_TRANSITIONS: readonly PrototypeTransition[] = ['dissolve', 'smart-animate'];
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
    <h3>Interactions</h3>
    @if (entries().length) {
      <label
        >Interaction
        <select
          aria-label="Prototype interaction"
          [value]="selectedIndex()"
          (change)="selectInteraction($event)"
        >
          @for (entry of entries(); track $index) {
            <option [value]="$index" [selected]="$index === selectedIndex()">
              {{ $index + 1 }}. {{ triggerLabel(entry.trigger) }} · {{ actionLabel(entry.action) }}
            </option>
          }
        </select>
      </label>
    }
    <div class="interaction-buttons">
      <button
        type="button"
        aria-label="Add interaction"
        [disabled]="!!node().swapBound || entries().length >= 16"
        (click)="addInteraction()"
      >
        Add
      </button>
      <button
        type="button"
        aria-label="Remove interaction"
        [disabled]="!!node().swapBound || !entries().length"
        (click)="removeInteraction()"
      >
        Remove
      </button>
      <button
        type="button"
        aria-label="Move interaction up"
        [disabled]="!!node().swapBound || selectedIndex() <= 0"
        (click)="moveInteraction(-1)"
      >
        Move up
      </button>
      <button
        type="button"
        aria-label="Move interaction down"
        [disabled]="!!node().swapBound || selectedIndex() >= entries().length - 1"
        (click)="moveInteraction(1)"
      >
        Move down
      </button>
    </div>
    <label
      >Trigger
      <select
        aria-label="Prototype trigger"
        [value]="currentInteraction()?.trigger ?? 'click'"
        [disabled]="!!node().swapBound || !currentInteraction()"
        (change)="trigger($event)"
      >
        @for (choice of triggers; track choice) {
          <option
            [value]="choice"
            [selected]="(currentInteraction()?.trigger ?? 'click') === choice"
          >
            {{ triggerLabel(choice) }}
          </option>
        }
      </select>
    </label>
    @if (currentInteraction()?.trigger === 'delay') {
      <label
        >Delay (ms)<input
          type="number"
          min="1"
          max="60000"
          step="1"
          aria-label="Prototype delay"
          [value]="delayValue()"
          [disabled]="!!node().swapBound"
          (change)="delay($event)"
      /></label>
    }
    <label
      >Action
      <select
        aria-label="Prototype action"
        [value]="editingInteraction()?.action ?? 'none'"
        [disabled]="!!node().swapBound"
        (change)="action($event)"
      >
        <option value="none">None</option>
        <option value="navigate" [disabled]="!screens().length">Navigate to screen</option>
        <option value="back">Back</option>
        <option value="open-overlay" [disabled]="!screens().length">Open overlay</option>
        <option value="close-overlay">Close overlay</option>
        <option value="change-state">Change component state</option>
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
    }
    @if (stateInteraction()) {
      <p>
        Change the nearest component instance during Play. Use its declared variant axes and values.
        Nested copied instances are not supported.
      </p>
      @for (entry of stateEntries(); track $index) {
        <label
          >Variant axis
          @if (stateAxes().length) {
            <select [attr.aria-label]="'State axis ' + ($index + 1)" [value]="entry[0]"
              [disabled]="!!node().swapBound" (change)="stateField($event, $index, 'name')">
              @if (!stateAxis(entry[0])) { <option [value]="entry[0]" [selected]="true" disabled>{{ entry[0] }} (unavailable)</option> }
              @for (axis of stateAxes(); track axis.name) {
                <option [value]="axis.name" [selected]="axis.name === entry[0]">{{ axis.name }}</option>
              }
            </select>
          } @else {
            <input [attr.aria-label]="'State axis ' + ($index + 1)" [value]="entry[0]"
              [disabled]="!!node().swapBound" (change)="stateField($event, $index, 'name')" />
          }
        </label>
        <label
          >Value
          @if (stateAxis(entry[0]); as axis) {
            <select [attr.aria-label]="'State value ' + ($index + 1)" [value]="entry[1]"
              [disabled]="!!node().swapBound" (change)="stateField($event, $index, 'value')">
              @if (!axis.values.includes(entry[1])) { <option [value]="entry[1]" [selected]="true" disabled>{{ entry[1] }} (unavailable)</option> }
              @for (value of axis.values; track value) {
                <option [value]="value" [selected]="value === entry[1]">{{ value }}</option>
              }
            </select>
          } @else {
            <input [attr.aria-label]="'State value ' + ($index + 1)" [value]="entry[1]"
              [disabled]="!!node().swapBound" (change)="stateField($event, $index, 'value')" />
          }
        </label>
        <button
          type="button"
          [attr.aria-label]="'Remove state axis ' + ($index + 1)"
          [disabled]="!!node().swapBound || stateEntries().length <= 1"
          (click)="removeStateAxis($index)"
        >
          Remove axis
        </button>
      }
      <button
        type="button"
        aria-label="Add state axis"
        [disabled]="!!node().swapBound || stateEntries().length >= 8"
        (click)="addStateAxis()"
      >
        Add axis
      </button>
    }
    @if (animated()) {
      <label
        >Transition
        <select
          aria-label="Prototype transition"
          [value]="animated()?.transition ?? 'instant'"
          [disabled]="!!node().swapBound"
          (change)="transition($event)"
        >
          <option value="instant" [selected]="!animated()?.transition">Instant</option>
          @for (choice of transitions(); track choice) {
            <option [value]="choice" [selected]="animated()?.transition === choice">
              {{ transitionLabel(choice) }}
            </option>
          }
        </select></label
      >
      @if (animated()?.transition) {
        <label
          >Duration (ms)
          <input
            type="number"
            min="1"
            max="2000"
            step="1"
            aria-label="Prototype duration"
            [value]="animated()?.durationMs ?? 300"
            [disabled]="!!node().swapBound"
            (change)="duration($event)"
          />
        </label>
      }
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
      Interactions navigate between root frames, position overlays, close them or go back. Play the
      flow from the toolbar.
    </p>
    <p>
      The first interaction for each trigger wins; later entries with the same trigger are ignored.
      Delay starts after the screen settles and runs once per visit.
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
    .interaction-buttons {
      display: flex;
      flex-wrap: wrap;
      gap: 6px;
    }
    button {
      font: inherit;
      padding: 5px 7px;
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
  /** Optional suggestions from the enclosing component; values remain native-validated. */
  readonly stateAxes = input<readonly VariantAxis[]>([]);
  readonly save = input.required<(id: string, patch: NodePatch) => boolean>();
  private readonly pending = signal<NodePatch | null>(null);
  protected readonly error = signal('');
  private readonly selected = signal(0);
  private nodeId = '';
  protected readonly triggers: readonly PrototypeTrigger[] = ['click', 'hover', 'press', 'delay'];
  protected readonly value = computed(() => ({ ...this.node(), ...this.pending() }));
  protected readonly entries = computed<readonly PrototypeTriggeredInteraction[]>(() => {
    const value = this.value();
    return value.interactions ?? (value.interaction ? [value.interaction] : []);
  });
  protected readonly selectedIndex = computed(() =>
    Math.max(0, Math.min(this.selected(), this.entries().length - 1)),
  );
  protected readonly currentInteraction = computed(
    () => this.entries()[this.selectedIndex()] ?? null,
  );
  // Existing action controls edit the action independently of the selected trigger.
  protected readonly editingInteraction = computed<PrototypeInteraction | null>(() => {
    const entry = this.currentInteraction();
    if (!entry) return null;
    const { trigger: _trigger, delayMs: _delay, ...action } = entry;
    return { ...action, trigger: 'click' } as PrototypeInteraction;
  });
  protected readonly delayValue = computed(() => {
    const entry = this.currentInteraction();
    return entry?.trigger === 'delay' ? entry.delayMs : 300;
  });
  protected readonly linked = computed(() => {
    const interaction = this.editingInteraction();
    return interaction?.action === 'navigate' || interaction?.action === 'open-overlay'
      ? interaction
      : null;
  });
  protected readonly stateInteraction = computed(() => {
    const value = this.editingInteraction();
    return value?.action === 'change-state' ? value : null;
  });
  protected readonly stateEntries = computed(() =>
    Object.entries(this.stateInteraction()?.values ?? {}),
  );
  protected readonly animated = computed(() => this.linked() ?? this.stateInteraction());
  protected readonly destination = computed(() => {
    return this.linked()?.target ?? '';
  });
  protected readonly outsideDismiss = computed(() => {
    const interaction = this.linked();
    return interaction?.action !== 'open-overlay' || interaction.dismissOutside !== false;
  });
  protected readonly transitions = computed<readonly PrototypeTransition[]>(() =>
    this.linked()?.action === 'open-overlay'
      ? OVERLAY_TRANSITIONS
      : this.stateInteraction()
        ? STATE_TRANSITIONS
        : NAVIGATE_TRANSITIONS,
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
      const node = this.node();
      if (this.nodeId !== node.id) {
        this.nodeId = node.id;
        this.selected.set(0);
      }
      this.pending.set(null);
      this.error.set('');
    });
  }
  protected action(event: Event): void {
    const select = event.target as HTMLSelectElement;
    if (this.node().swapBound) {
      select.value = this.editingInteraction()?.action ?? 'none';
      return;
    }
    const value = select.value;
    let interaction: PrototypeInteraction | null | undefined;
    if (value === 'none') interaction = null;
    else if (value === 'back' || value === 'close-overlay')
      interaction = { trigger: 'click', action: value };
    else if (value === 'change-state') {
      const axis = this.stateAxes()[0];
      const previous = this.stateInteraction();
      interaction = previous ?? {
        trigger: 'click',
        action: 'change-state',
        values: { [axis?.name ?? 'state']: axis?.default ?? axis?.values[0] ?? 'default' },
      };
    } else if ((value === 'navigate' || value === 'open-overlay') && this.screens().length) {
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
      select.value = this.editingInteraction()?.action ?? 'none';
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
      interaction = this.animated();
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
      interaction = this.animated(),
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
  protected stateField(event: Event, index: number, field: 'name' | 'value'): void {
    const control = event.target as HTMLInputElement;
    const entries = this.stateEntries().map(([name, value]) => [name, value] as [string, string]);
    const current = entries[index];
    if (!current || this.node().swapBound) return;
    const old = current[field === 'name' ? 0 : 1];
    current[field === 'name' ? 0 : 1] = control.value;
    const axis=this.stateAxis(current[0]);
    if(field==='name' && axis && !axis.values.includes(current[1])) current[1]=axis.default;
    if (!this.saveStateEntries(entries)) control.value = old;
  }
  protected stateAxis(name: string): VariantAxis | undefined {
    return this.stateAxes().find((axis)=>axis.name===name);
  }
  protected addStateAxis(): void {
    const entries = this.stateEntries();
    if (this.node().swapBound || !this.stateInteraction() || entries.length >= 8) return;
    const suggested = this.stateAxes().find(
      (axis) => !entries.some(([name]) => name === axis.name),
    );
    let name = suggested?.name ?? 'state';
    for (let n = 2; entries.some(([existing]) => existing === name); n++) name = `state${n}`;
    this.saveStateEntries([
      ...entries,
      [name, suggested?.default ?? suggested?.values[0] ?? 'default'],
    ]);
  }
  protected removeStateAxis(index: number): void {
    if (this.node().swapBound || this.stateEntries().length <= 1) return;
    this.saveStateEntries(this.stateEntries().filter((_, at) => at !== index));
  }
  private saveStateEntries(entries: readonly [string, string][]): boolean {
    const interaction = this.stateInteraction();
    const bytes = (value: string) => new TextEncoder().encode(value).length;
    if (
      !interaction ||
      this.node().swapBound ||
      entries.length < 1 ||
      entries.length > 8 ||
      new Set(entries.map(([name]) => name)).size !== entries.length ||
      entries.some(
        ([name, value]) => !name.length || bytes(name) > 64 || !value.length || bytes(value) > 128,
      )
    )
      return false;
    return this.change({ interaction: { ...interaction, values: Object.fromEntries(entries) } });
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
  protected triggerLabel(trigger: PrototypeTrigger): string {
    return { click: 'On click', hover: 'On hover', press: 'On press', delay: 'After delay' }[
      trigger
    ];
  }
  protected actionLabel(action: PrototypeInteraction['action']): string {
    return {
      navigate: 'Navigate',
      back: 'Back',
      'open-overlay': 'Open overlay',
      'close-overlay': 'Close overlay',
      'change-state': 'Change state',
    }[action];
  }
  protected selectInteraction(event: Event): void {
    const index = Number((event.target as HTMLSelectElement).value);
    if (Number.isInteger(index) && index >= 0 && index < this.entries().length)
      this.selected.set(index);
  }
  protected addInteraction(): void {
    if (this.node().swapBound || this.entries().length >= 16) return;
    const entries = [...this.entries()];
    const trigger =
      this.triggers.find((choice) => !entries.some((entry) => entry.trigger === choice)) ?? 'click';
    const action: PrototypeInteraction = this.screens().length
      ? { trigger: 'click', action: 'navigate', target: this.screens()[0].id }
      : { trigger: 'click', action: 'back' };
    const entry = this.withTrigger(action, trigger, 300);
    if (this.change({ interactions: [...entries, entry] })) this.selected.set(entries.length);
  }
  protected removeInteraction(): void {
    if (this.node().swapBound || !this.entries().length) return;
    const entries = [...this.entries()];
    entries.splice(this.selectedIndex(), 1);
    if (this.change({ interactions: entries }))
      this.selected.set(Math.min(this.selectedIndex(), entries.length - 1));
  }
  protected moveInteraction(direction: -1 | 1): void {
    if (this.node().swapBound) return;
    const entries = [...this.entries()],
      index = this.selectedIndex(),
      next = index + direction;
    if (next < 0 || next >= entries.length) return;
    [entries[index], entries[next]] = [entries[next], entries[index]];
    if (this.change({ interactions: entries })) this.selected.set(next);
  }
  protected trigger(event: Event): void {
    const control = event.target as HTMLSelectElement;
    const trigger = control.value as PrototypeTrigger;
    const entry = this.currentInteraction(),
      action = this.editingInteraction();
    if (!entry || !action || this.node().swapBound || !this.triggers.includes(trigger)) {
      control.value = entry?.trigger ?? 'click';
      return;
    }
    if (entry.trigger === trigger) return;
    const entries = [...this.entries()];
    entries[this.selectedIndex()] = this.withTrigger(action, trigger, this.delayValue());
    if (!this.change({ interactions: entries })) control.value = entry.trigger;
  }
  protected delay(event: Event): void {
    const control = event.target as HTMLInputElement,
      entry = this.currentInteraction();
    const delayMs = Number(control.value);
    if (
      !entry ||
      entry.trigger !== 'delay' ||
      this.node().swapBound ||
      !Number.isInteger(delayMs) ||
      delayMs < 1 ||
      delayMs > 60000
    ) {
      control.value = String(this.delayValue());
      return;
    }
    const entries = [...this.entries()];
    entries[this.selectedIndex()] = { ...entry, delayMs };
    if (!this.change({ interactions: entries })) control.value = String(entry.delayMs);
  }
  private withTrigger(
    action: PrototypeInteraction,
    trigger: PrototypeTrigger,
    delayMs: number,
  ): PrototypeTriggeredInteraction {
    const { trigger: _trigger, ...base } = action;
    return (
      trigger === 'delay' ? { ...base, trigger, delayMs } : { ...base, trigger }
    ) as PrototypeTriggeredInteraction;
  }
  private change(patch: NodePatch): boolean {
    if ('interaction' in patch && this.value().interactions !== undefined) {
      const entries = [...this.entries()],
        entry = this.currentInteraction();
      if (patch.interaction)
        entries[this.selectedIndex()] = this.withTrigger(
          patch.interaction,
          entry?.trigger ?? 'click',
          this.delayValue(),
        );
      else entries.splice(this.selectedIndex(), 1);
      patch = { interactions: entries };
    }
    if (this.save()(this.node().id, patch)) {
      // Mirror native atomic replacement so pending legacy data cannot hide a list.
      const pending =
        'interactions' in patch
          ? { ...patch, interaction: null }
          : 'interaction' in patch
            ? { ...patch, interactions: undefined }
            : patch;
      this.pending.update((old) => ({ ...old, ...pending }));
      this.error.set('');
      return true;
    }
    this.error.set('The prototype edit was refused. The saved prototype settings were kept.');
    return false;
  }
}
