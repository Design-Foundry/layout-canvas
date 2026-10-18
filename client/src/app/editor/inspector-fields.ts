import { Component, ElementRef, computed, effect, inject, input, output, signal } from '@angular/core';
import { FormControl, ReactiveFormsModule } from '@angular/forms';
import { ColorValue, Length, OklchColor, TokenRow, TokenType, TokenValue } from '@canvas/bridge';
import { LucentIconButton, LucentPrefix, LucentReferenceTag, LucentTextInput } from '@design-foundry/lucent';
import { NumberField } from './number-field';
import { formatMagnitude, lengthInPx } from './token-model';

export function isToken(value: unknown): value is TokenValue {
  return typeof value === 'object' && value !== null && 'token' in value;
}

/** The field asks to bind a token: the element to anchor the picker to. */
export type PickRequest = ElementRef<HTMLElement>;

/**
 * A number that can also be a dimension or opacity token: a numeric field with a link button, or the
 * token's tag with its resolved value and a detach button.
 */
@Component({
  selector: 'app-value-field',
  imports: [NumberField, LucentIconButton, LucentReferenceTag],
  template: `
    @if (token(); as token) {
      <button type="button" class="bound" [disabled]="disabled()" (click)="pick.emit(host)">
        <lucent-reference-tag kind="token" [icon]="icon() || 'token'" [label]="token.identifier" />
        <span class="bound-value typography-caption">{{ token.text }}</span>
      </button>
      @if (!disabled()) {
        <lucent-icon-button icon="link_off" size="sm" variant="ghost" ariaLabel="Detach token" (clicked)="detach()" />
      }
    } @else {
      <app-number-field
        [label]="label()"
        [icon]="icon()"
        [suffix]="suffix()"
        [value]="shown()"
        [min]="min()"
        [max]="max()"
        [optional]="optional()"
        [placeholder]="placeholder()"
        [disabled]="disabled()"
        (commit)="commitNumber($event)"
      />
      @if (bindable() && !disabled()) {
        <lucent-icon-button
          class="link"
          icon="link"
          size="sm"
          variant="ghost"
          [ariaLabel]="'Apply ' + family() + ' token'"
          (clicked)="pick.emit(host)"
        />
      }
    }
  `,
  styleUrl: './inspector-fields.scss',
})
export class ValueField {
  readonly value = input.required<Length | null | undefined>();
  readonly label = input.required<string>();
  readonly icon = input('');
  readonly suffix = input('');
  readonly min = input(-Infinity);
  readonly max = input(Infinity);
  /** Multiplies the stored number for display, e.g. 100 for percentages. */
  readonly scale = input(1);
  readonly optional = input(false);
  readonly placeholder = input('');
  readonly disabled = input(false);
  readonly family = input<'dimension' | 'opacity'>('dimension');
  readonly tokens = input<readonly TokenRow[]>([]);
  /** Shows the link button. */
  readonly bindable = input(true);

  /** A new literal, or null when an optional field is cleared. */
  readonly commit = output<Length | null>();
  readonly pick = output<PickRequest>();

  protected readonly host = inject(ElementRef<HTMLElement>);

  protected readonly token = computed(() => {
    const value = this.value();
    if (!isToken(value)) return null;
    const row = this.tokens().find((t) => t.id === value.token);
    const resolved = row?.resolved && 'value' in row.resolved ? row.resolved : null;
    return { identifier: row?.identifier ?? 'missing token', text: resolved ? formatMagnitude(resolved) : '', resolved };
  });

  protected readonly shown = computed(() => {
    const value = this.value();
    return typeof value === 'number' ? value * this.scale() : null;
  });

  protected commitNumber(value: number | null): void {
    this.commit.emit(value === null ? null : value / this.scale());
  }

  protected detach(): void {
    const resolved = this.token()?.resolved;
    if (!resolved) return;
    this.commit.emit(this.family() === 'dimension' ? lengthInPx(resolved) : resolved.value);
  }
}

/**
 * A color that can also be a color token: swatch with the native picker (live preview), hex and alpha,
 * or the token's tag with its resolved hex and a detach button.
 */
@Component({
  selector: 'app-color-field',
  imports: [ReactiveFormsModule, NumberField, LucentIconButton, LucentReferenceTag, LucentTextInput, LucentPrefix],
  template: `
    @if (token(); as token) {
      <button type="button" class="bound" (click)="pick.emit(host)">
        <lucent-reference-tag kind="token" [label]="token.identifier" [swatch]="token.hex" />
        <span class="bound-value typography-caption">{{ token.text }}</span>
      </button>
      <lucent-icon-button icon="link_off" size="sm" variant="ghost" ariaLabel="Detach token" (clicked)="detach()" />
    } @else if (color(); as color) {
      <div class="color">
        <label class="swatch" [style.background]="hex()">
          <input
            type="color"
            class="picker"
            [value]="hex()"
            (input)="previewHex($event, color.a)"
            (change)="commitPreview.emit()"
          />
        </label>
        <lucent-text-input
          size="sm"
          [formControl]="hexControl"
          (focusin)="editing.set(true)"
          (focusout)="commitHex(color)"
          (keydown.enter)="blur($event)"
          (keydown.escape)="cancelHex($event)"
        >
          <span lucentPrefix class="typography-caption">#</span>
        </lucent-text-input>
        <app-number-field
          [label]="label() + ' opacity'"
          icon="opacity"
          suffix="%"
          [value]="color.a * 100"
          [min]="0"
          [max]="100"
          (commit)="commitAlpha($event, color)"
        />
      </div>
      <lucent-icon-button icon="link" size="sm" variant="ghost" ariaLabel="Apply color token" (clicked)="pick.emit(host)" />
    }
  `,
  styleUrl: './inspector-fields.scss',
})
export class ColorField {
  readonly value = input.required<ColorValue>();
  readonly label = input('Color');
  readonly tokens = input<readonly TokenRow[]>([]);
  readonly toHex = input.required<(color: OklchColor) => string>();
  readonly fromHex = input.required<(hex: string, alpha: number) => OklchColor | null>();

  readonly commit = output<ColorValue>();
  readonly preview = output<ColorValue>();
  readonly commitPreview = output<void>();
  readonly pick = output<PickRequest>();

  protected readonly host = inject(ElementRef<HTMLElement>);
  protected readonly hexControl = new FormControl('', { nonNullable: true });
  protected readonly editing = signal(false);

  protected readonly color = computed(() => {
    const value = this.value();
    return isToken(value) ? null : value;
  });
  protected readonly hex = computed(() => {
    const color = this.color();
    return color ? this.toHex()(color) : '';
  });
  protected readonly token = computed(() => {
    const value = this.value();
    if (!isToken(value)) return null;
    const row = this.tokens().find((t) => t.id === value.token);
    const resolved = row?.resolved && 'hex' in row.resolved ? row.resolved : null;
    return {
      identifier: row?.identifier ?? 'missing token',
      hex: resolved?.hex ?? 'transparent',
      text: resolved ? (resolved.a < 1 ? `${resolved.hex} ${Math.round(resolved.a * 100)}%` : resolved.hex) : '',
      resolved,
    };
  });

  constructor() {
    effect(() => {
      const hex = this.hex();
      if (hex && !this.editing()) this.hexControl.setValue(hex.slice(1));
    });
  }

  protected previewHex(event: Event, alpha: number): void {
    const color = this.fromHex()((event.target as HTMLInputElement).value, alpha);
    if (color) this.preview.emit(color);
  }

  protected commitHex(current: OklchColor): void {
    this.editing.set(false);
    const text = this.hexControl.value.trim().replace(/^#/, '').toLowerCase();
    const color = this.fromHex()(text, current.a);
    if (color && `#${text}` !== this.hex()) {
      this.commit.emit(color);
    } else {
      this.hexControl.setValue(this.hex().slice(1));
    }
  }

  protected cancelHex(event: Event): void {
    this.hexControl.setValue(this.hex().slice(1));
    this.blur(event);
  }

  protected commitAlpha(percent: number | null, color: OklchColor): void {
    if (percent !== null) this.commit.emit({ l: color.l, c: color.c, h: color.h, a: percent / 100 });
  }

  protected detach(): void {
    const resolved = this.token()?.resolved;
    if (resolved) this.commit.emit({ l: resolved.l, c: resolved.c, h: resolved.h, a: resolved.a });
  }

  protected blur(event: Event): void {
    (event.target as HTMLElement).blur();
  }
}

/** Token families a field key accepts. */
export type BindableFamily = Extract<TokenType, 'color' | 'dimension' | 'opacity' | 'border' | 'shadow'>;
