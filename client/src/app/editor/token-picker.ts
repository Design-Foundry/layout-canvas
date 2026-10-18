import { Component, ElementRef, afterNextRender, computed, input, output, viewChild } from '@angular/core';
import { toSignal } from '@angular/core/rxjs-interop';
import { FormControl, ReactiveFormsModule } from '@angular/forms';
import { TokenRow } from '@canvas/bridge';
import { LucentIcon, LucentPrefix, LucentTextInput } from '@design-foundry/lucent';
import { formatMagnitude } from './token-model';

/** A searchable list of tokens. Hovering one previews it; clicking picks it. */
@Component({
  selector: 'app-token-picker',
  imports: [ReactiveFormsModule, LucentTextInput, LucentPrefix, LucentIcon],
  template: `
    <div class="picker" (keydown.escape)="dismiss.emit()">
      <lucent-text-input #search size="sm" placeholder="Search tokens" [formControl]="query">
        <lucent-icon lucentPrefix name="search" size="xs" />
      </lucent-text-input>
      <div class="list" role="listbox" (mouseleave)="hover.emit(null)">
        @for (token of matches(); track token.id) {
          <button
            type="button"
            role="option"
            class="token typography-body-sm"
            [class.selected]="token.id === selectedId()"
            [attr.aria-selected]="token.id === selectedId()"
            (mouseenter)="hover.emit(token.id)"
            (focus)="hover.emit(token.id)"
            (click)="pick.emit(token.id)"
          >
            @if (token.type === 'color') {
              <span class="swatch" [style.background]="swatch(token)"></span>
            } @else {
              <span class="value">{{ magnitude(token) }}</span>
            }
            <span class="name">{{ token.identifier }}</span>
          </button>
        } @empty {
          <span class="empty typography-body-sm">No matching tokens</span>
        }
      </div>
    </div>
  `,
  styleUrls: ['./token-row.scss', './token-picker.scss'],
})
export class TokenPicker {
  readonly tokens = input.required<readonly TokenRow[]>();
  readonly selectedId = input<string | null>(null);
  /** The token under the pointer or keyboard focus, or null when it leaves the list. */
  readonly hover = output<string | null>();
  readonly pick = output<string>();
  readonly dismiss = output<void>();

  protected readonly query = new FormControl('', { nonNullable: true });
  private readonly queryText = toSignal(this.query.valueChanges, { initialValue: '' });
  private readonly search = viewChild.required('search', { read: ElementRef });

  protected readonly matches = computed(() => {
    const text = this.queryText().trim().toLowerCase();
    return this.tokens().filter(
      (t) => !text || t.identifier.includes(text) || (t.displayName ?? '').toLowerCase().includes(text),
    );
  });

  constructor() {
    afterNextRender(() => this.search().nativeElement.querySelector('input')?.focus());
  }

  protected swatch(token: TokenRow): string {
    return token.resolved && 'hex' in token.resolved ? token.resolved.hex : 'transparent';
  }

  protected magnitude(token: TokenRow): string {
    return token.resolved && 'value' in token.resolved ? formatMagnitude(token.resolved) : '';
  }
}
