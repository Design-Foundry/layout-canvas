import { Component, computed, effect, input, signal, untracked } from '@angular/core';
import type { NodePatch, NodeProps } from '@canvas/bridge';
import { NumberField } from './number-field';

type Point = { readonly x: number; readonly y: number };

/** Normalize anchors and control points together, keeping their parent coordinates unchanged. */
export function normalizePath(
  points: readonly Point[],
  closed: boolean,
  inHandles: readonly Point[] = [],
  outHandles: readonly Point[] = [],
  filled = false,
): NodePatch {
  const all = [...points, ...inHandles, ...outHandles];
  const x = Math.min(...all.map((p) => p.x)),
    y = Math.min(...all.map((p) => p.y));
  const w = Math.max(...all.map((p) => p.x)) - x,
    h = Math.max(...all.map((p) => p.y)) - y;
  const normalized = (items: readonly Point[]) =>
    items.map((p) => ({ x: w ? (p.x - x) / w : 0, y: h ? (p.y - y) / h : 0 }));
  return {
    x,
    y,
    w,
    h,
    path: {
      closed,
      points: normalized(points),
      ...(inHandles.length
        ? { inHandles: normalized(inHandles), outHandles: normalized(outHandles) }
        : {}),
      ...(filled && closed ? { filled: true } : {}),
    },
  };
}

const midpoint = (a: Point, b: Point): Point => ({ x: (a.x + b.x) / 2, y: (a.y + b.y) / 2 });

@Component({
  selector: 'app-path-panel',
  imports: [NumberField],
  template: `<section aria-label="Path geometry">
    <h3>Path</h3>
    <fieldset [disabled]="!!node().inFlow">
      <label
        >Vertex
        <select aria-label="Path vertex" [value]="selected()" (change)="select($event)">
          @for (p of points(); track $index) {
            <option [value]="$index">{{ $index + 1 }}</option>
          }
        </select></label
      >
      <div class="pair">
        <app-number-field label="Vertex X" [value]="point().x" (commit)="coordinate(0, $event)" />
        <app-number-field label="Vertex Y" [value]="point().y" (commit)="coordinate(1, $event)" />
      </div>
      <label
        >Vertex type
        <select
          aria-label="Vertex type"
          [value]="curved() ? 'curve' : 'corner'"
          (change)="setVertexType($event)"
        >
          <option value="corner">Corner</option>
          <option value="curve">Curve</option>
        </select>
      </label>
      @if (curved()) {
        <div class="pair">
          <app-number-field
            label="Incoming X"
            [value]="incoming()[selected()].x"
            (commit)="control('in', 'x', $event)"
          />
          <app-number-field
            label="Incoming Y"
            [value]="incoming()[selected()].y"
            (commit)="control('in', 'y', $event)"
          />
        </div>
        <div class="pair">
          <app-number-field
            label="Outgoing X"
            [value]="outgoing()[selected()].x"
            (commit)="control('out', 'x', $event)"
          />
          <app-number-field
            label="Outgoing Y"
            [value]="outgoing()[selected()].y"
            (commit)="control('out', 'y', $event)"
          />
        </div>
      }
      <div class="pair">
        <button type="button" [disabled]="points().length >= 64" (click)="add()">Add vertex</button>
        <button type="button" [disabled]="points().length <= (closed() ? 3 : 2)" (click)="remove()">
          Remove vertex
        </button>
      </div>
      <label
        ><input
          type="checkbox"
          [checked]="closed()"
          [disabled]="points().length < 3"
          (change)="toggleClosed($event)"
        />
        Closed path</label
      >
    </fieldset>
    <label
      ><input
        type="checkbox"
        aria-label="Filled path"
        [checked]="filled()"
        [disabled]="!closed()"
        (change)="toggleFilled($event)"
      />
      Filled path</label
    >
    <app-number-field
      label="Line width"
      [value]="value().lineWidth ?? 2"
      [min]="0"
      [max]="4096"
      (commit)="width($event)"
    />
    <p>
      Pen (P): click corners or drag curves. Enter or double-click finishes; Escape cancels. Pencil
      (Shift+P) draws freehand. Drag control handles on source paths; Alt moves one handle
      independently.
    </p>
    @if (node().inFlow) {
      <p>Vertex positions follow auto layout. Move the path out of the flow to edit them.</p>
    }
    @if (node().inInstance) {
      <p>Edit copied vertices here; these changes remain instance overrides.</p>
    }
    @if (error()) {
      <p role="alert">{{ error() }}</p>
    }
  </section>`,
  styles: `
    section,
    fieldset {
      display: grid;
      gap: 10px;
    }
    section {
      padding: 12px;
      border-bottom: 1px solid #333;
    }
    fieldset {
      border: 0;
      margin: 0;
      padding: 0;
      min-width: 0;
    }
    .pair {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 8px;
    }
    h3,
    p {
      margin: 0;
      font: inherit;
    }
    p {
      color: #aaa;
      font-size: 12px;
    }
    select,
    button {
      color: inherit;
      background: #292929;
      border: 1px solid #555;
      padding: 5px;
    }
    [role='alert'] {
      color: #ffbd83;
    }
  `,
})
export class PathPanel {
  readonly node = input.required<NodeProps>();
  readonly save = input.required<(id: string, patch: NodePatch) => boolean>();
  private readonly pending = signal<NodePatch | null>(null);
  protected readonly selected = signal(0);
  protected readonly error = signal('');
  protected readonly value = computed(() => ({ ...this.node(), ...this.pending() }));
  protected readonly closed = computed(() => this.value().path?.closed ?? false);
  protected readonly filled = computed(() => this.value().path?.filled ?? false);
  private world(items: readonly Point[]): readonly Point[] {
    const n = this.value();
    return items.map((p) => ({ x: n.x + p.x * n.w, y: n.y + p.y * n.h }));
  }
  protected readonly incoming = computed(() => this.world(this.value().path?.inHandles ?? []));
  protected readonly outgoing = computed(() => this.world(this.value().path?.outHandles ?? []));
  protected readonly curved = computed(() => {
    const i = this.selected(),
      p = this.points()[i],
      a = this.incoming()[i],
      b = this.outgoing()[i];
    return !!p && !!a && !!b && (a.x !== p.x || a.y !== p.y || b.x !== p.x || b.y !== p.y);
  });
  protected readonly points = computed<readonly Point[]>(() => {
    const n = this.value();
    return (n.path?.points ?? []).map((p) => ({ x: n.x + p.x * n.w, y: n.y + p.y * n.h }));
  });
  protected readonly point = computed(
    () => this.points()[Math.min(this.selected(), this.points().length - 1)] ?? { x: 0, y: 0 },
  );
  constructor() {
    let previousId: string | undefined;
    effect(() => {
      const node = this.node();
      this.pending.set(null);
      this.error.set('');
      this.selected.set(
        previousId === node.id
          ? Math.min(untracked(this.selected), (node.path?.points.length ?? 1) - 1)
          : 0,
      );
      previousId = node.id;
    });
  }
  protected select(event: Event): void {
    this.selected.set(Number((event.target as HTMLSelectElement).value));
  }
  protected coordinate(axis: 0 | 1, value: number | null): void {
    if (value === null || !Number.isFinite(value) || this.node().inFlow) return;
    const index = this.selected(),
      old = this.point(),
      next = { x: axis === 0 ? value : old.x, y: axis === 1 ? value : old.y };
    const move = (items: readonly Point[]) =>
      items.map((p, i) => (i === index ? { x: p.x + next.x - old.x, y: p.y + next.y - old.y } : p));
    this.geometry(
      this.points().map((p, i) => (i === index ? next : p)),
      this.closed(),
      move(this.incoming()),
      move(this.outgoing()),
    );
  }
  protected control(side: 'in' | 'out', axis: 'x' | 'y', value: number | null): void {
    if (value === null || !Number.isFinite(value) || this.node().inFlow) return;
    const incoming = [...this.incoming()],
      outgoing = [...this.outgoing()],
      controls = side === 'in' ? incoming : outgoing;
    controls[this.selected()] = { ...controls[this.selected()], [axis]: value };
    this.geometry(this.points(), this.closed(), incoming, outgoing);
  }
  protected setVertexType(event: Event): void {
    const target = event.target as HTMLSelectElement;
    if (this.node().inFlow) {
      target.value = this.curved() ? 'curve' : 'corner';
      return;
    }
    const index = this.selected(),
      points = this.points(),
      p = points[index];
    const incoming = [...(this.incoming().length ? this.incoming() : points)],
      outgoing = [...(this.outgoing().length ? this.outgoing() : points)];
    if (target.value === 'corner') {
      incoming[index] = p;
      outgoing[index] = p;
    } else {
      const prev = points[index - 1] ?? (this.closed() ? points.at(-1)! : p),
        next = points[index + 1] ?? (this.closed() ? points[0] : p);
      const dx = next.x - prev.x,
        dy = next.y - prev.y,
        distance = Math.hypot(dx, dy),
        scale = distance ? 1 / 6 : 0;
      incoming[index] = { x: p.x - (distance ? dx * scale : 20), y: p.y - dy * scale };
      outgoing[index] = { x: p.x + (distance ? dx * scale : 20), y: p.y + dy * scale };
    }
    const corners = points.every(
      (p, i) =>
        incoming[i].x === p.x &&
        incoming[i].y === p.y &&
        outgoing[i].x === p.x &&
        outgoing[i].y === p.y,
    );
    if (!this.geometry(points, this.closed(), corners ? [] : incoming, corners ? [] : outgoing))
      target.value = this.curved() ? 'curve' : 'corner';
  }
  protected add(): void {
    if (this.node().inFlow || this.points().length >= 64) return;
    const points = [...this.points()],
      incoming = [...this.incoming()],
      outgoing = [...this.outgoing()],
      index = this.selected();
    const nextIndex = index + 1 < points.length ? index + 1 : this.closed() ? 0 : index;
    const p = points[index],
      q = points[nextIndex];
    let added = midpoint(p, q);
    if (incoming.length) {
      if (nextIndex !== index) {
        const a = midpoint(p, outgoing[index]),
          b = midpoint(outgoing[index], incoming[nextIndex]),
          c = midpoint(incoming[nextIndex], q),
          d = midpoint(a, b),
          e = midpoint(b, c);
        added = midpoint(d, e);
        outgoing[index] = a;
        incoming[nextIndex] = c;
        incoming.splice(index + 1, 0, d);
        outgoing.splice(index + 1, 0, e);
      } else {
        incoming.splice(index + 1, 0, added);
        outgoing.splice(index + 1, 0, added);
      }
    }
    points.splice(index + 1, 0, added);
    if (this.geometry(points, this.closed(), incoming, outgoing)) this.selected.set(index + 1);
  }
  protected remove(): void {
    if (this.node().inFlow || this.points().length <= (this.closed() ? 3 : 2)) return;
    const points = [...this.points()],
      incoming = [...this.incoming()],
      outgoing = [...this.outgoing()];
    points.splice(this.selected(), 1);
    if (incoming.length) {
      incoming.splice(this.selected(), 1);
      outgoing.splice(this.selected(), 1);
    }
    if (this.geometry(points, this.closed(), incoming, outgoing))
      this.selected.update((i) => Math.min(i, points.length - 1));
  }
  protected toggleClosed(event: Event): void {
    const target = event.target as HTMLInputElement;
    if (
      this.node().inFlow ||
      (target.checked && this.points().length < 3) ||
      !this.geometry(this.points(), target.checked)
    )
      target.checked = this.closed();
  }
  protected toggleFilled(event: Event): void {
    const target = event.target as HTMLInputElement;
    if (
      (!this.closed() && target.checked) ||
      !this.change({ path: { ...this.value().path!, filled: target.checked } })
    )
      target.checked = this.filled();
  }
  protected width(value: number | null): void {
    if (value !== null && Number.isFinite(value) && value >= 0 && value <= 4096)
      this.change({ lineWidth: value });
  }
  private geometry(
    points: readonly Point[],
    closed: boolean,
    incoming = this.incoming(),
    outgoing = this.outgoing(),
  ): boolean {
    return this.change(normalizePath(points, closed, incoming, outgoing, this.filled()));
  }
  private change(patch: NodePatch): boolean {
    if (this.save()(this.node().id, patch)) {
      this.pending.update((old) => ({ ...old, ...patch }));
      this.error.set('');
      return true;
    }
    this.error.set('The path edit was refused. The saved geometry was kept.');
    return false;
  }
}
