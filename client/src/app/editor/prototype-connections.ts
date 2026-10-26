import { Component, ElementRef, computed, effect, inject, input, signal } from '@angular/core';
import type { NodePatch, NodeProps, PrototypeTriggeredInteraction, Rect } from '@canvas/bridge';

export interface PrototypeConnectionTarget {
  readonly id: string;
  readonly name: string;
  /** Bounds in stage-local CSS pixels, like VisualCore.screenBounds. */
  readonly bounds: Rect;
}

/** Append rather than silently replacing an existing trigger or its animation options. */
export function appendPrototypeConnection(node: NodeProps, target: string): NodePatch {
  if (!target) throw new Error('Choose a destination screen.');
  if (node.type === 'instance' && !node.inInstance)
    throw new Error('Select a layer inside the instance to connect it.');
  const entries: readonly PrototypeTriggeredInteraction[] =
    node.interactions ?? (node.interaction ? [node.interaction] : []);
  if (entries.some((entry) => entry.trigger === 'click'))
    throw new Error('This layer already has a click interaction. Edit it in the Prototype inspector.');
  if (entries.length >= 16) throw new Error('A layer can have at most 16 interactions.');
  return { interactions: [...entries, { trigger: 'click', action: 'navigate', target }] };
}

export function prototypeConnectionPath(source: Rect, target: Rect): string {
  const x = source.x + source.w, y = source.y + source.h / 2;
  const tx = target.x, ty = target.y + target.h / 2;
  const dx = Math.max(40, Math.abs(tx - x) / 2);
  return `M ${x} ${y} C ${x + dx} ${y}, ${tx - dx} ${ty}, ${tx} ${ty}`;
}

@Component({
  selector: 'app-prototype-connections',
  host: {
    '(pointermove)': 'move($event)', '(pointerup)': 'finish($event)',
    '(pointercancel)': 'cancel()', '(lostpointercapture)': 'cancel()',
    '(document:keydown.escape)': 'cancel()',
  },
  template: `
    @if (node() && source()) {
      <svg aria-hidden="true">
        <defs><marker id="prototype-connection-arrow" markerWidth="7" markerHeight="7"
          refX="6" refY="3.5" orient="auto"><polygon points="0,0 7,3.5 0,7" fill="#688cff" /></marker></defs>
        @for (wire of wires(); track wire.key) {
          <path [attr.d]="wire.path" marker-end="url(#prototype-connection-arrow)" />
        }
        @if (drag(); as position) {
          @for (target of targets(); track target.id) {
            <rect [attr.x]="target.bounds.x" [attr.y]="target.bounds.y"
              [attr.width]="target.bounds.w" [attr.height]="target.bounds.h"
              [class.active]="hovered() === target.id" />
          }
          <path [attr.d]="preview()" marker-end="url(#prototype-connection-arrow)" />
        }
      </svg>
      <button class="handle" type="button" aria-label="Drag prototype connection"
        title="Drag to a screen to add a click connection"
        [style.left.px]="source()!.x + source()!.w" [style.top.px]="source()!.y + source()!.h / 2"
        (pointerdown)="begin($event)" (click)="showControls.set(true)">+</button>
      @if (showControls() || drag()) {
        <div class="connection-controls" (keydown)="$event.stopPropagation()">
          <label>Connect to
            <select aria-label="Connection destination" [value]="choice()" (change)="choose($event)">
              <option value="">Choose screen</option>
              @for (target of targets(); track target.id) {
                <option [value]="target.id">{{ target.name || 'Untitled frame' }}</option>
              }
            </select>
          </label>
          <button type="button" [disabled]="!choice()" (click)="connect(choice())">Add click connection</button>
          <button type="button" (click)="hide()">Close</button>
          <span role="status">{{ message() }}</span>
        </div>
      }
    }
  `,
  styles: `
    :host { position:absolute; inset:0; pointer-events:none; }
    svg { position:absolute; width:100%; height:100%; overflow:hidden; }
    path { fill:none; stroke:#688cff; stroke-width:2; }
    rect { fill:#688cff12; stroke:#688cff; stroke-dasharray:5 4; }
    rect.active { fill:#688cff33; stroke-width:3; }
    button, select { font:inherit; }
    .handle { position:absolute; transform:translate(-50%,-50%); border:2px solid white;
      width:24px; height:24px; border-radius:50%; background:#466be0; color:white;
      pointer-events:auto; cursor:crosshair; touch-action:none; }
    .connection-controls { position:absolute; bottom:12px; left:12px; display:flex; flex-wrap:wrap;
      align-items:center; gap:8px; max-width:calc(100% - 24px); padding:8px;
      background:var(--lucent-surface, #242424); color:var(--lucent-on-surface, #eee);
      border:1px solid #688cff; border-radius:6px; pointer-events:auto; font-size:12px; }
    .connection-controls label { display:flex; align-items:center; gap:6px; }
    .connection-controls span { flex-basis:100%; }
  `,
})
export class PrototypeConnections {
  readonly node = input<NodeProps | null>(null);
  readonly source = input<Rect | null>(null);
  readonly targets = input<readonly PrototypeConnectionTarget[]>([]);
  readonly save = input.required<(id: string, patch: NodePatch) => boolean>();
  protected readonly showControls = signal(false);
  protected readonly choice = signal('');
  protected readonly message = signal('');
  protected readonly drag = signal<{ x: number; y: number } | null>(null);
  private readonly host = inject(ElementRef<HTMLElement>);
  private pointer: number | null = null;
  private draggingNode = '';
  private sourceId = '';
  protected readonly hovered = computed(() => {
    const point = this.drag();
    return point ? [...this.targets()].reverse().find(({ bounds: b }) =>
      point.x >= b.x && point.x <= b.x + b.w && point.y >= b.y && point.y <= b.y + b.h)?.id ?? '' : '';
  });
  protected readonly preview = computed(() => {
    const source = this.source(), point = this.drag();
    return source && point ? prototypeConnectionPath(source, { ...point, w: 0, h: 0 }) : '';
  });
  protected readonly wires = computed(() => {
    const node = this.node(), source = this.source();
    if (!node || !source) return [];
    const entries = node.interactions ?? (node.interaction ? [node.interaction] : []);
    return entries.flatMap((entry, index) => {
      if (entry.action !== 'navigate' && entry.action !== 'open-overlay') return [];
      const target = this.targets().find((item) => item.id === entry.target);
      return target ? [{ key: index, path: prototypeConnectionPath(source, target.bounds) }] : [];
    });
  });
  constructor() {
    effect(() => {
      const id = this.node()?.id ?? '';
      if (id !== this.sourceId) { this.cancel(); this.hide(); this.choice.set(''); this.sourceId = id; }
    });
  }
  protected choose(event: Event): void { this.choice.set((event.target as HTMLSelectElement).value); }
  protected begin(event: PointerEvent): void {
    if (event.button !== 0 || !event.isPrimary || !this.node() || !this.source()) return;
    event.preventDefault(); event.stopPropagation();
    this.pointer = event.pointerId;
    this.draggingNode = this.node()!.id;
    (event.target as HTMLElement).setPointerCapture(event.pointerId);
    this.message.set('Drop on a screen. Escape cancels.');
    this.move(event);
  }
  protected move(event: PointerEvent): void {
    if (this.pointer !== event.pointerId) return;
    const bounds = this.host.nativeElement.getBoundingClientRect();
    this.drag.set({ x: event.clientX - bounds.left, y: event.clientY - bounds.top });
  }
  protected finish(event: PointerEvent): void {
    if (this.pointer !== event.pointerId) return;
    if (this.node()?.id !== this.draggingNode) { this.cancel(); return; }
    this.move(event);
    const target = this.hovered();
    this.cancel();
    this.showControls.set(true);
    if (target) this.connect(target);
    else this.message.set('No screen selected. Choose a destination or drag again.');
  }
  protected cancel(): void { this.pointer = null; this.draggingNode = ''; this.drag.set(null); }
  protected hide(): void { this.cancel(); this.showControls.set(false); this.message.set(''); }
  protected connect(target: string): void {
    const node = this.node();
    if (!node || !this.targets().some((item) => item.id === target)) {
      this.message.set('Choose an available destination screen.'); return;
    }
    try {
      const patch = appendPrototypeConnection(node, target);
      this.message.set(this.save()(node.id, patch) ? 'Click connection added.' : 'Connection refused. No changes saved.');
    } catch (error) { this.message.set((error as Error).message); }
  }
}
