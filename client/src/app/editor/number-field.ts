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
      [formControl]="control"
      (focusin)="focused.set(true)"
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
  readonly value = input.required<number>();
  readonly suffix = input('');
  readonly min = input(-Infinity);
  readonly max = input(Infinity);
  readonly commit = output<number>();

  protected readonly control = new FormControl('', { nonNullable: true });
  protected readonly focused = signal(false);

  constructor() {
    effect(() => {
      const value = this.value();
      if (!this.focused()) this.control.setValue(format(value));
    });
  }

  protected onBlur(): void {
    this.focused.set(false);
    const text = this.control.value.trim();
    if (text !== format(this.value())) this.apply(Number(text));
  }

  protected onKeydown(event: KeyboardEvent): void {
    const target = event.target as HTMLElement;
    if (event.key === 'Enter') {
      target.blur();
    } else if (event.key === 'Escape') {
      this.control.setValue(format(this.value()));
      target.blur();
    } else if (event.key === 'ArrowUp' || event.key === 'ArrowDown') {
      event.preventDefault();
      const current = Number(this.control.value.trim());
      const step = (event.shiftKey ? BIG_STEP : STEP) * (event.key === 'ArrowUp' ? 1 : -1);
      this.apply((Number.isFinite(current) ? current : this.value()) + step);
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

function format(value: number): string {
  return String(Math.round(value * 100) / 100);
}
