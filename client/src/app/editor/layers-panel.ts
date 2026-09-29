import { Component, input, output } from '@angular/core';
import { CdkFixedSizeVirtualScroll, CdkVirtualForOf, CdkVirtualScrollViewport } from '@angular/cdk/scrolling';
import { LucentListItem } from '@design-foundry/lucent';
import { LAYER_ICONS, LayerRow } from './editor-model';

const ROW_HEIGHT = 32;
const INDENT = 12;

@Component({
  selector: 'app-layers-panel',
  imports: [CdkVirtualScrollViewport, CdkFixedSizeVirtualScroll, CdkVirtualForOf, LucentListItem],
  template: `
    <h2 class="title typography-label-md">Layers</h2>
    <cdk-virtual-scroll-viewport class="viewport" [itemSize]="rowHeight">
      <lucent-list-item
        *cdkVirtualFor="let layer of layers(); trackBy: trackById"
        class="row"
        [icon]="icons[layer.kind]"
        [selected]="selectedIds().has(layer.id)"
        [style.padding-inline-start.px]="baseIndent + layer.depth * indent"
        (activated)="select.emit(layer.id)"
      >
        {{ layer.name }}
      </lucent-list-item>
    </cdk-virtual-scroll-viewport>
  `,
  styleUrl: './panel.scss',
})
export class LayersPanel {
  readonly layers = input.required<readonly LayerRow[]>();
  readonly selectedIds = input<ReadonlySet<string>>(new Set());
  readonly select = output<string>();

  protected readonly icons = LAYER_ICONS;
  protected readonly rowHeight = ROW_HEIGHT;
  protected readonly indent = INDENT;
  protected readonly baseIndent = 8;

  protected trackById(_: number, layer: LayerRow): string {
    return layer.id;
  }
}
