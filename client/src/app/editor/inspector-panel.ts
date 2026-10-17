import { Component, computed, effect, input, output, signal } from '@angular/core';
import { takeUntilDestroyed } from '@angular/core/rxjs-interop';
import { FormControl, ReactiveFormsModule } from '@angular/forms';
import { NodePatch, NodeProps, OklchColor } from '@canvas/bridge';
import { LucentCheckbox, LucentPrefix, LucentTextInput } from '@design-foundry/lucent';
import { NumberField } from './number-field';

@Component({
  selector: 'app-inspector-panel',
  imports: [ReactiveFormsModule, LucentTextInput, LucentPrefix, LucentCheckbox, NumberField],
  template: `
    <h2 class="title typography-label-md">Design</h2>
    @if (node(); as node) {
      <section class="section">
        <h3 class="heading typography-caption">Position</h3>
        <div class="pair">
          <app-number-field label="X" [value]="node.x" (commit)="edit.emit({ x: $event })" />
          <app-number-field label="Y" [value]="node.y" (commit)="edit.emit({ y: $event })" />
        </div>
      </section>

      <section class="section">
        <h3 class="heading typography-caption">Size</h3>
        <div class="pair">
          <app-number-field label="W" [value]="node.w" [min]="0" (commit)="edit.emit({ w: $event })" />
          <app-number-field label="H" [value]="node.h" [min]="0" (commit)="edit.emit({ h: $event })" />
        </div>
      </section>

      <section class="section">
        <h3 class="heading typography-caption">Appearance</h3>
        <div class="pair">
          <app-number-field
            label="Opacity"
            icon="opacity"
            suffix="%"
            [value]="node.opacity * 100"
            [min]="0"
            [max]="100"
            (commit)="edit.emit({ opacity: $event / 100 })"
          />
          @if (node.type !== 'ellipse') {
            @if (radius(); as radius) {
              <app-number-field
                label="Corner radius"
                icon="rounded_corner"
                [value]="radius.value"
                [min]="0"
                (commit)="edit.emit({ cornerRadius: $event })"
              />
            } @else {
              <lucent-text-input size="sm" [readonly]="true" [formControl]="radiusTokenControl" />
            }
          }
        </div>
      </section>

      <section class="section">
        <h3 class="heading typography-caption">Fill</h3>
        @if (fill(); as fill) {
          <div class="fill">
            <label class="swatch" [style.background]="fill.hex">
              <input
                type="color"
                class="picker"
                [value]="fill.hex"
                (input)="previewColor($event, fill)"
                (change)="commitPreview.emit()"
              />
            </label>
            <lucent-text-input
              size="sm"
              class="hex"
              [formControl]="hexControl"
              (focusin)="hexFocused.set(true)"
              (focusout)="commitHex(fill)"
              (keydown.enter)="blur($event)"
              (keydown.escape)="cancelHex($event, fill)"
            >
              <span lucentPrefix class="typography-caption">#</span>
            </lucent-text-input>
            <app-number-field
              label="Fill opacity"
              icon="opacity"
              suffix="%"
              [value]="fill.a * 100"
              [min]="0"
              [max]="100"
              (commit)="commitAlpha($event, fill)"
            />
          </div>
        } @else {
          <lucent-text-input size="sm" [readonly]="true" [formControl]="fillTokenControl" />
        }
      </section>

      @if (node.type === 'frame') {
        <section class="section">
          <lucent-checkbox size="sm" label="Clip content" [formControl]="clipControl" />
        </section>
      }
    }
  `,
  styleUrls: ['./panel.scss', './inspector-panel.scss'],
})
export class InspectorPanel {
  readonly node = input<NodeProps | null>(null);
  readonly colorFromHex = input.required<(hex: string, alpha: number) => OklchColor | null>();
  readonly edit = output<NodePatch>();
  /** A live change from a continuous control. The page shows it without recording it. */
  readonly previewEdit = output<NodePatch>();
  /** The continuous control was released: the last preview becomes an edit. */
  readonly commitPreview = output<void>();

  protected readonly fill = computed(() => {
    const fill = this.node()?.fill;
    return fill && 'hex' in fill ? fill : null;
  });
  protected readonly radius = computed(() => {
    const radius = this.node()?.cornerRadius;
    return typeof radius === 'number' ? { value: radius } : null;
  });

  protected readonly hexControl = new FormControl('', { nonNullable: true });
  protected readonly hexFocused = signal(false);
  protected readonly clipControl = new FormControl(false, { nonNullable: true });
  protected readonly fillTokenControl = new FormControl('', { nonNullable: true });
  protected readonly radiusTokenControl = new FormControl('', { nonNullable: true });

  constructor() {
    this.clipControl.valueChanges
      .pipe(takeUntilDestroyed())
      .subscribe((clipContent) => this.edit.emit({ clipContent }));
    effect(() => {
      const node = this.node();
      if (!node) return;
      const fill = this.fill();
      if (fill && !this.hexFocused()) this.hexControl.setValue(fill.hex.slice(1));
      if (!fill && 'token' in node.fill) this.fillTokenControl.setValue(node.fill.token);
      if (typeof node.cornerRadius !== 'number') this.radiusTokenControl.setValue(node.cornerRadius.token);
      this.clipControl.setValue(node.clipContent, { emitEvent: false });
    });
  }

  protected blur(event: Event): void {
    (event.target as HTMLElement).blur();
  }

  protected commitHex(fill: OklchColor & { hex: string }): void {
    this.hexFocused.set(false);
    const text = this.hexControl.value.trim();
    const color = this.colorFromHex()(text, fill.a);
    if (color && `#${text.replace(/^#/, '').toLowerCase()}` !== fill.hex) {
      this.edit.emit({ fill: color });
    } else {
      this.hexControl.setValue(fill.hex.slice(1));
    }
  }

  protected cancelHex(event: Event, fill: OklchColor & { hex: string }): void {
    this.hexControl.setValue(fill.hex.slice(1));
    this.blur(event);
  }

  protected previewColor(event: Event, fill: OklchColor & { hex: string }): void {
    const color = this.colorFromHex()((event.target as HTMLInputElement).value, fill.a);
    if (color) this.previewEdit.emit({ fill: color });
  }

  protected commitAlpha(percent: number, fill: OklchColor): void {
    this.edit.emit({ fill: { l: fill.l, c: fill.c, h: fill.h, a: percent / 100 } });
  }
}
