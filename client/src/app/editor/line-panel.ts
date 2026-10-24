import { Component, computed, effect, input, signal } from '@angular/core';
import type { NodePatch, NodeProps } from '@canvas/bridge';
import { NumberField } from './number-field';

@Component({
  selector: 'app-line-panel',
  imports: [NumberField],
  template: `<section aria-label="Line geometry">
    <h3>Line</h3>
    <fieldset [disabled]="!!node().inFlow">
      <div class="pair">
        <app-number-field
          label="Start X"
          [value]="points().x1"
          (commit)="endpoint('x1', $event)"
        /><app-number-field
          label="Start Y"
          [value]="points().y1"
          (commit)="endpoint('y1', $event)"
        />
      </div>
      <div class="pair">
        <app-number-field
          label="End X"
          [value]="points().x2"
          (commit)="endpoint('x2', $event)"
        /><app-number-field label="End Y" [value]="points().y2" (commit)="endpoint('y2', $event)" />
      </div>
    </fieldset>
    <app-number-field
      label="Line width"
      [value]="value().lineWidth ?? 2"
      [min]="0"
      [max]="4096"
      (commit)="width($event)"
    />
    <p>
      Round caps. Drag either endpoint on the canvas. Shift: 45° steps. Alt: resize from center.
    </p>
    @if (node().inFlow) {
      <p>Endpoint positions follow auto layout. Move the line out of the flow to edit them.</p>
    }
    @if (error()) {
      <p role="alert">{{ error() }}</p>
    }
  </section>`,
  styles: `
    section {
      padding: 12px;
      display: grid;
      gap: 10px;
      border-bottom: 1px solid #333;
    }
    h3,
    p {
      margin: 0;
      font: inherit;
    }
    p {
      font-size: 12px;
      color: #aaa;
    }
    fieldset {
      border: 0;
      padding: 0;
      margin: 0;
      display: grid;
      gap: 10px;
      min-width: 0;
    }
    .pair {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 8px;
    }
    [role='alert'] {
      color: #ffbd83;
    }
  `,
})
export class LinePanel {
  readonly node = input.required<NodeProps>();
  readonly save = input.required<(id: string, patch: NodePatch) => boolean>();
  private readonly pending = signal<NodePatch | null>(null);
  protected readonly error = signal('');
  protected readonly value = computed(() => ({ ...this.node(), ...this.pending() }));
  protected readonly points = computed(() => {
    const n = this.value();
    return {
      x1: n.x,
      y1: n.y + (n.lineReverse ? n.h : 0),
      x2: n.x + n.w,
      y2: n.y + (n.lineReverse ? 0 : n.h),
    };
  });
  constructor() {
    effect(() => {
      this.node();
      this.pending.set(null);
      this.error.set('');
    });
  }
  protected endpoint(key: 'x1' | 'y1' | 'x2' | 'y2', value: number | null): void {
    if (value === null || this.node().inFlow) return;
    const p = { ...this.points(), [key]: value };
    this.change({
      x: Math.min(p.x1, p.x2),
      y: Math.min(p.y1, p.y2),
      w: Math.abs(p.x2 - p.x1),
      h: Math.abs(p.y2 - p.y1),
      lineReverse: (p.x2 - p.x1) * (p.y2 - p.y1) < 0,
    });
  }
  protected width(value: number | null): void {
    if (value !== null) this.change({ lineWidth: value });
  }
  private change(patch: NodePatch): void {
    if (this.save()(this.node().id, patch)) {
      this.pending.update((old) => ({ ...old, ...patch }));
      this.error.set('');
    } else this.error.set('The line edit was refused. The saved geometry was kept.');
  }
}
