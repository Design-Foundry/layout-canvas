import { Component, computed, effect, input, signal } from '@angular/core';
import type { ImageCrop, ImageFit, NodePatch, NodeProps } from '@canvas/bridge';
import { NumberField } from './number-field';

export function validImageCrop(crop: ImageCrop): boolean {
  return (
    Object.values(crop).every(Number.isFinite) &&
    crop.x >= 0 &&
    crop.y >= 0 &&
    crop.w > 0 &&
    crop.h > 0 &&
    crop.x + crop.w <= 1 &&
    crop.y + crop.h <= 1
  );
}
@Component({
  selector: 'app-image-panel',
  imports: [NumberField],
  template: `<section aria-label="Image geometry">
    <h3>Image</h3>
    <p>{{ node().image?.width }} × {{ node().image?.height }} embedded pixels</p>
    <p class="id" title="Content SHA-256">{{ node().image?.id }}</p>
    <div class="pair">
      <app-number-field
        label="X"
        [value]="node().box.x"
        [disabled]="!!node().inFlow"
        (commit)="dimension('x', $event)"
      />
      <app-number-field
        label="Y"
        [value]="node().box.y"
        [disabled]="!!node().inFlow"
        (commit)="dimension('y', $event)"
      />
    </div>
    <div class="pair">
      <app-number-field
        label="W"
        [value]="node().box.w"
        [min]="0"
        (commit)="dimension('w', $event)"
      />
      <app-number-field
        label="H"
        [value]="node().box.h"
        [min]="0"
        (commit)="dimension('h', $event)"
      />
    </div>
    <label
      >Image fit
      <select aria-label="Image fit" [value]="value().imageFit ?? 'cover'" (change)="fit($event)">
        <option value="cover">Cover</option>
        <option value="contain">Contain</option>
        <option value="stretch">Stretch</option>
      </select></label
    >
    <h4>Source crop (0–1)</h4>
    <div class="pair">
      <app-number-field
        label="Crop X"
        [value]="crop().x"
        [min]="0"
        [max]="1"
        (commit)="cropValue('x', $event)"
      />
      <app-number-field
        label="Crop Y"
        [value]="crop().y"
        [min]="0"
        [max]="1"
        (commit)="cropValue('y', $event)"
      />
    </div>
    <div class="pair">
      <app-number-field
        label="Crop W"
        [value]="crop().w"
        [min]="0"
        [max]="1"
        (commit)="cropValue('w', $event)"
      />
      <app-number-field
        label="Crop H"
        [value]="crop().h"
        [min]="0"
        [max]="1"
        (commit)="cropValue('h', $event)"
      />
    </div>
    <button type="button" (click)="resetCrop()">Reset crop</button>
    <p>Fit uses this source region inside a fixed image box. Opacity applies to the whole image.</p>
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
    h4,
    p {
      margin: 0;
      font: inherit;
    }
    p {
      font-size: 12px;
      color: #aaa;
    }
    .id {
      overflow-wrap: anywhere;
      font-family: monospace;
    }
    .pair {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 8px;
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
export class ImagePanel {
  readonly node = input.required<NodeProps>();
  readonly save = input.required<(id: string, patch: NodePatch) => boolean>();
  private readonly pending = signal<NodePatch | null>(null);
  protected readonly error = signal('');
  protected readonly value = computed(() => ({ ...this.node(), ...this.pending() }));
  protected readonly crop = computed(() => this.value().imageCrop ?? { x: 0, y: 0, w: 1, h: 1 });
  constructor() {
    effect(() => {
      this.node();
      this.pending.set(null);
      this.error.set('');
    });
  }
  protected dimension(key: 'x' | 'y' | 'w' | 'h', value: number | null): void {
    if (
      value === null ||
      !Number.isFinite(value) ||
      ((key === 'x' || key === 'y') && this.node().inFlow)
    )
      return;
    this.change({
      [key]: value,
      ...(key === 'w'
        ? { widthSizing: 'fixed' as const }
        : key === 'h'
          ? { heightSizing: 'fixed' as const }
          : {}),
    });
  }
  protected fit(event: Event): void {
    const target = event.target as HTMLSelectElement;
    if (
      !['cover', 'contain', 'stretch'].includes(target.value) ||
      !this.change({ imageFit: target.value as ImageFit })
    )
      target.value = this.value().imageFit ?? 'cover';
  }
  protected cropValue(key: keyof ImageCrop, value: number | null): void {
    if (value === null) return;
    const crop = { ...this.crop(), [key]: value };
    if (!validImageCrop(crop)) {
      this.error.set('Crop must stay inside the image and have positive width and height.');
      return;
    }
    this.change({ imageCrop: crop });
  }
  protected resetCrop(): void {
    this.change({ imageCrop: { x: 0, y: 0, w: 1, h: 1 } });
  }
  private change(patch: NodePatch): boolean {
    if (this.save()(this.node().id, patch)) {
      this.pending.update((old) => ({ ...old, ...patch }));
      this.error.set('');
      return true;
    }
    this.error.set('The image edit was refused. The saved image was kept.');
    return false;
  }
}
