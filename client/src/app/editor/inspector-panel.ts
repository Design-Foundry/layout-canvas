import { CdkConnectedOverlay, CdkOverlayOrigin, ConnectedPosition } from '@angular/cdk/overlay';
import { Component, computed, effect, input, output, signal, viewChild } from '@angular/core';
import { takeUntilDestroyed } from '@angular/core/rxjs-interop';
import { FormControl, ReactiveFormsModule } from '@angular/forms';
import { NodePatch, NodeProps, OklchColor, TokenRow } from '@canvas/bridge';
import {
  LucentCheckbox,
  LucentIconButton,
  LucentPrefix,
  LucentReferenceTag,
  LucentTextInput,
} from '@design-foundry/lucent';
import { NumberField } from './number-field';
import { formatMagnitude, lengthInPx } from './token-model';
import { TokenPicker } from './token-picker';

type BindableProp = 'fill' | 'cornerRadius';

const PICKER_POSITIONS: ConnectedPosition[] = [
  { originX: 'end', originY: 'bottom', overlayX: 'end', overlayY: 'top', offsetY: 4 },
  { originX: 'end', originY: 'top', overlayX: 'end', overlayY: 'bottom', offsetY: -4 },
];

@Component({
  selector: 'app-inspector-panel',
  imports: [
    ReactiveFormsModule,
    CdkConnectedOverlay,
    CdkOverlayOrigin,
    LucentTextInput,
    LucentPrefix,
    LucentCheckbox,
    LucentIconButton,
    LucentReferenceTag,
    NumberField,
    TokenPicker,
  ],
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
          <div class="bindable" cdkOverlayOrigin #radiusOrigin="cdkOverlayOrigin">
            @if (radiusToken(); as token) {
              <button type="button" class="bound" (click)="openPicker('cornerRadius')">
                <lucent-reference-tag kind="token" icon="rounded_corner" [label]="token.identifier" />
                <span class="bound-value typography-caption">{{ token.text }}</span>
              </button>
              <lucent-icon-button
                icon="link_off"
                size="sm"
                variant="ghost"
                ariaLabel="Detach token"
                (clicked)="detachRadius()"
              />
            } @else {
              <app-number-field
                label="Corner radius"
                icon="rounded_corner"
                [value]="radius()"
                [min]="0"
                (commit)="edit.emit({ cornerRadius: $event })"
              />
              <lucent-icon-button
                icon="link"
                size="sm"
                variant="ghost"
                ariaLabel="Apply dimension token"
                (clicked)="openPicker('cornerRadius')"
              />
            }
          </div>
        }
      </section>

      <section class="section">
        <h3 class="heading typography-caption">Fill</h3>
        <div class="bindable" cdkOverlayOrigin #fillOrigin="cdkOverlayOrigin">
          @if (fillToken(); as token) {
            <button type="button" class="bound" (click)="openPicker('fill')">
              <lucent-reference-tag kind="token" [label]="token.identifier" [swatch]="token.hex" />
              <span class="bound-value typography-caption">{{ token.text }}</span>
            </button>
            <lucent-icon-button
              icon="link_off"
              size="sm"
              variant="ghost"
              ariaLabel="Detach token"
              (clicked)="detachFill()"
            />
          } @else if (fill(); as fill) {
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
            <lucent-icon-button
              icon="link"
              size="sm"
              variant="ghost"
              ariaLabel="Apply color token"
              (clicked)="openPicker('fill')"
            />
          }
        </div>
      </section>

      @if (node.type === 'frame') {
        <section class="section">
          <lucent-checkbox size="sm" label="Clip content" [formControl]="clipControl" />
        </section>
      }
    }

    <ng-template
      cdkConnectedOverlay
      [cdkConnectedOverlayOrigin]="pickerOrigin()!"
      [cdkConnectedOverlayOpen]="!!pickerOrigin()"
      [cdkConnectedOverlayPositions]="positions"
      (overlayOutsideClick)="closePicker()"
      (detach)="closePicker()"
    >
      <app-token-picker
        [tokens]="pickerTokens()"
        [selectedId]="boundId(picking())"
        (hover)="previewToken($event)"
        (pick)="pickToken($event)"
        (dismiss)="closePicker()"
      />
    </ng-template>
  `,
  styleUrls: ['./panel.scss', './inspector-panel.scss'],
})
export class InspectorPanel {
  readonly node = input<NodeProps | null>(null);
  /** The document's tokens, resolved in the active mode. */
  readonly tokens = input<readonly TokenRow[]>([]);
  readonly colorFromHex = input.required<(hex: string, alpha: number) => OklchColor | null>();
  readonly edit = output<NodePatch>();
  /** A live change from a continuous control. The page shows it without recording it. */
  readonly previewEdit = output<NodePatch>();
  /** The continuous control was released: the last preview becomes an edit. */
  readonly commitPreview = output<void>();
  /** The continuous control was abandoned: the last preview is dropped. */
  readonly cancelPreview = output<void>();

  protected readonly positions = PICKER_POSITIONS;
  protected readonly picking = signal<BindableProp | null>(null);
  private readonly fillOrigin = viewChild<CdkOverlayOrigin>('fillOrigin');
  private readonly radiusOrigin = viewChild<CdkOverlayOrigin>('radiusOrigin');
  private previewing = false;

  protected readonly fill = computed(() => {
    const fill = this.node()?.fill;
    return fill && 'hex' in fill ? fill : null;
  });
  protected readonly radius = computed(() => {
    const radius = this.node()?.cornerRadius;
    return typeof radius === 'number' ? radius : 0;
  });
  protected readonly fillToken = computed(() => {
    const fill = this.node()?.fill;
    if (!fill || !('token' in fill)) return null;
    const token = this.tokens().find((t) => t.id === fill.token);
    const resolved = token?.resolved && 'hex' in token.resolved ? token.resolved : null;
    return {
      identifier: token?.identifier ?? 'missing token',
      hex: resolved?.hex ?? 'transparent',
      text: resolved ? (resolved.a < 1 ? `${resolved.hex} ${Math.round(resolved.a * 100)}%` : resolved.hex) : '',
    };
  });
  protected readonly radiusToken = computed(() => {
    const radius = this.node()?.cornerRadius;
    if (radius === undefined || typeof radius === 'number') return null;
    const token = this.tokens().find((t) => t.id === radius.token);
    const resolved = token?.resolved && 'value' in token.resolved ? token.resolved : null;
    return { identifier: token?.identifier ?? 'missing token', text: resolved ? formatMagnitude(resolved) : '' };
  });
  protected readonly pickerOrigin = computed(() => {
    const picking = this.picking();
    if (picking === 'fill') return this.fillOrigin();
    if (picking === 'cornerRadius') return this.radiusOrigin();
    return undefined;
  });
  protected readonly pickerTokens = computed(() => {
    const type = this.picking() === 'fill' ? 'color' : 'dimension';
    return this.tokens().filter((t) => t.type === type);
  });

  protected readonly hexControl = new FormControl('', { nonNullable: true });
  protected readonly hexFocused = signal(false);
  protected readonly clipControl = new FormControl(false, { nonNullable: true });

  constructor() {
    this.clipControl.valueChanges
      .pipe(takeUntilDestroyed())
      .subscribe((clipContent) => this.edit.emit({ clipContent }));
    effect(() => {
      const node = this.node();
      if (!node) return;
      const fill = this.fill();
      if (fill && !this.hexFocused()) this.hexControl.setValue(fill.hex.slice(1));
      this.clipControl.setValue(node.clipContent, { emitEvent: false });
    });
  }

  protected boundId(prop: BindableProp | null): string | null {
    const value = prop ? this.node()?.[prop] : undefined;
    return value !== undefined && typeof value === 'object' && 'token' in value ? value.token : null;
  }

  protected openPicker(prop: BindableProp): void {
    this.picking.set(this.picking() === prop ? null : prop);
  }

  protected previewToken(id: string | null): void {
    const prop = this.picking();
    if (!prop) return;
    if (id) {
      this.previewing = true;
      this.previewEdit.emit({ [prop]: { token: id } });
    } else if (this.previewing) {
      this.previewing = false;
      this.cancelPreview.emit();
    }
  }

  protected pickToken(id: string): void {
    const prop = this.picking();
    if (!prop) return;
    this.previewEdit.emit({ [prop]: { token: id } });
    this.previewing = false;
    this.commitPreview.emit();
    this.picking.set(null);
  }

  protected closePicker(): void {
    if (this.previewing) {
      this.previewing = false;
      this.cancelPreview.emit();
    }
    this.picking.set(null);
  }

  protected detachFill(): void {
    const token = this.tokens().find((t) => t.id === this.boundId('fill'));
    const resolved = token?.resolved && 'hex' in token.resolved ? token.resolved : null;
    if (resolved) this.edit.emit({ fill: { l: resolved.l, c: resolved.c, h: resolved.h, a: resolved.a } });
  }

  protected detachRadius(): void {
    const token = this.tokens().find((t) => t.id === this.boundId('cornerRadius'));
    const resolved = token?.resolved && 'value' in token.resolved ? token.resolved : null;
    if (resolved) this.edit.emit({ cornerRadius: lengthInPx(resolved) });
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
