import { Component, input, output, signal } from '@angular/core';
import { FormControl, ReactiveFormsModule } from '@angular/forms';
import { CdkFixedSizeVirtualScroll, CdkVirtualForOf, CdkVirtualScrollViewport } from '@angular/cdk/scrolling';
import { LucentIconButton, LucentInlineInput, LucentListItem, LucentTrailing } from '@design-foundry/lucent';
import { LAYER_ICONS, LayerRow } from './editor-model';

const ROW_HEIGHT = 32;
const INDENT = 12;

export interface LayerRename {
  readonly id: string;
  readonly name: string;
}

export interface LayerVisibility {
  readonly id: string;
  readonly visible: boolean;
}

export interface LayerLock {
  readonly id: string;
  readonly locked: boolean;
}

@Component({
  selector: 'app-layers-panel',
  imports: [
    ReactiveFormsModule,
    CdkVirtualScrollViewport,
    CdkFixedSizeVirtualScroll,
    CdkVirtualForOf,
    LucentListItem,
    LucentInlineInput,
    LucentIconButton,
    LucentTrailing,
  ],
  template: `
    <cdk-virtual-scroll-viewport class="viewport" [itemSize]="rowHeight">
      <lucent-list-item
        *cdkVirtualFor="let layer of layers(); trackBy: trackById"
        class="row"
        [class.hidden]="!layer.visible"
        [icon]="icons[layer.kind]"
        [selected]="selectedIds().has(layer.id)"
        [style.padding-inline-start.px]="baseIndent + layer.depth * indent"
        (activated)="activate.emit(layer.id)"
        (dblclick)="startRename(layer)"
      >
        @if (renamingId() === layer.id) {
          <lucent-inline-input
            class="name"
            typography="typography-body-sm"
            [formControl]="nameControl"
            (focusout)="commitRename(layer)"
            (keydown.enter)="blur($event)"
            (keydown.escape)="cancelRename($event)"
            (click)="$event.stopPropagation()"
          />
        } @else {
          {{ layer.name }}
        }
        <lucent-icon-button
          lucentTrailing
          class="toggle lock"
          [class.on]="layer.locked"
          size="sm"
          variant="ghost"
          [icon]="layer.locked ? 'lock' : 'lock_open'"
          [ariaLabel]="layer.locked ? 'Unlock' : 'Lock'"
          (clicked)="lock.emit({ id: layer.id, locked: !layer.locked })"
          (click)="$event.stopPropagation()"
        />
        <lucent-icon-button
          lucentTrailing
          class="toggle visibility"
          [class.on]="!layer.visible"
          size="sm"
          variant="ghost"
          [icon]="layer.visible ? 'visibility' : 'visibility_off'"
          [ariaLabel]="layer.visible ? 'Hide' : 'Show'"
          (clicked)="visibility.emit({ id: layer.id, visible: !layer.visible })"
          (click)="$event.stopPropagation()"
        />
      </lucent-list-item>
    </cdk-virtual-scroll-viewport>
  `,
  styleUrl: './panel.scss',
  styles: `
    .row .toggle {
      opacity: 0;
    }
    .row:hover .toggle,
    .row:focus-within .toggle,
    .row .toggle.on {
      opacity: 1;
    }
    .row.hidden {
      color: var(--color--semantic--text-subtle);
    }
  `,
})
export class LayersPanel {
  readonly layers = input.required<readonly LayerRow[]>();
  readonly selectedIds = input<ReadonlySet<string>>(new Set());
  readonly activate = output<string>();
  readonly rename = output<LayerRename>();
  readonly visibility = output<LayerVisibility>();
  readonly lock = output<LayerLock>();

  protected readonly icons = LAYER_ICONS;
  protected readonly rowHeight = ROW_HEIGHT;
  protected readonly indent = INDENT;
  protected readonly baseIndent = 8;
  protected readonly renamingId = signal<string | null>(null);
  protected readonly nameControl = new FormControl('', { nonNullable: true });

  protected trackById(_: number, layer: LayerRow): string {
    return layer.id;
  }

  protected startRename(layer: LayerRow): void {
    this.nameControl.setValue(layer.name);
    this.renamingId.set(layer.id);
    setTimeout(() => {
      const field = document.querySelector<HTMLInputElement>('app-layers-panel .name input');
      field?.focus();
      field?.select();
    });
  }

  protected commitRename(layer: LayerRow): void {
    if (this.renamingId() !== layer.id) return;
    this.renamingId.set(null);
    const name = this.nameControl.value.trim();
    if (name && name !== layer.name) this.rename.emit({ id: layer.id, name });
  }

  protected cancelRename(event: Event): void {
    this.renamingId.set(null);
    (event.target as HTMLElement).blur();
  }

  protected blur(event: Event): void {
    (event.target as HTMLElement).blur();
  }
}
