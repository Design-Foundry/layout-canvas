import { Component, effect, input, output, signal } from '@angular/core';
import { FormControl, ReactiveFormsModule } from '@angular/forms';
import { LucentIcon, LucentPrefix, LucentSuffix, LucentTextInput } from '@design-foundry/lucent';

const STEP = 1;
const BIG_STEP = 10;

/** A compact numeric field: commits on Enter or blur, reverts on Escape, steps with the arrow keys. */
@Component({
  selector: 'app-number-field',
  imports: [ReactiveFormsModule, LucentTextInput, LucentPrefix, LucentSuffix, LucentIcon],
  template: `
    <lucent-text-input
      size="sm"
      [placeholder]="placeholder()"
      [readonly]="disabled()"
      [formControl]="control"
      (focusin)="onFocus()"
      (focusout)="onBlur()"
      (keydown)="onKeydown($event)"
    >
      @if (icon()) {
        <lucent-icon lucentPrefix class="label" size="xs" [name]="icon()" [ariaLabel]="label()" />
      } @else if (label()) {
        <span lucentPrefix class="label typography-caption">{{ label() }}</span>
      }
      @if (suffix()) {
        <span lucentSuffix class="typography-caption">{{ suffix() }}</span>
      }
    </lucent-text-input>
  `,
  styles: `
    :host {
      display: block;
      min-width: 0;
    }
    .label {
      color: var(--color--semantic--text-subtle);
      min-width: 1ch;
    }
  `,
})
export class NumberField {
  readonly label = input.required<string>();
  /** Shown instead of the label text; the label becomes its accessible name. */
  readonly icon = input('');
  /** Null shows an empty field, for optional values. */
  readonly value = input.required<number | null>();
  readonly placeholder = input('');
  readonly disabled = input(false);
  /** Lets the user clear the field; clearing emits null. */
  readonly optional = input(false);
  readonly suffix = input('');
  readonly min = input(-Infinity);
  readonly max = input(Infinity);
  readonly commit = output<number | null>();

  protected readonly control = new FormControl('', { nonNullable: true });
  protected readonly focused = signal(false);

  constructor() {
    effect(() => {
      const value = this.value();
      if (!this.focused() || !this.control.dirty) this.control.setValue(format(value));
    });
  }

  protected onFocus(): void {
    this.control.markAsPristine();
    this.focused.set(true);
  }

  protected onBlur(): void {
    this.focused.set(false);
    if (!this.control.dirty) return;
    const text = this.control.value.trim();
    if (text === format(this.value())) return;
    if (!text && this.optional()) {
      this.commit.emit(null);
      return;
    }
    this.apply(Number(text));
  }

  protected onKeydown(event: KeyboardEvent): void {
    const target = event.target as HTMLElement;
    if (event.key === 'Enter') {
      target.blur();
    } else if (event.key === 'Escape') {
      this.control.setValue(format(this.value()));
      this.control.markAsPristine();
      target.blur();
    } else if ((event.key === 'ArrowUp' || event.key === 'ArrowDown') && !this.disabled()) {
      event.preventDefault();
      const text = this.control.value.trim();
      const current = text ? Number(text) : (this.value() ?? 0);
      const step = (event.shiftKey ? BIG_STEP : STEP) * (event.key === 'ArrowUp' ? 1 : -1);
      this.apply((Number.isFinite(current) ? current : (this.value() ?? 0)) + step);
    }
  }

  private apply(next: number): void {
    if (!Number.isFinite(next)) {
      this.control.setValue(format(this.value()));
      return;
    }
    const clamped = Math.min(this.max(), Math.max(this.min(), next));
    this.control.setValue(format(clamped));
    if (clamped !== this.value()) this.commit.emit(clamped);
  }
}

function format(value: number | null): string {
  return value === null ? '' : String(Math.round(value * 100) / 100);
}
