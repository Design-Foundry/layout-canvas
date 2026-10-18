import { Component, computed, effect, input, output, signal } from '@angular/core';
import { takeUntilDestroyed } from '@angular/core/rxjs-interop';
import { FormControl, ReactiveFormsModule } from '@angular/forms';
import { ModeRow, TokenRow, TokenType } from '@canvas/bridge';
import {
  LucentIcon,
  LucentIconButton,
  LucentInlineInput,
  LucentMenu,
  LucentMenuItem,
  LucentMenuTrigger,
  LucentSelect,
} from '@design-foundry/lucent';
import { TOKEN_FAMILIES, formatMagnitude } from './token-model';

export interface ModeRename {
  readonly id: string;
  readonly name: string;
}

@Component({
  selector: 'app-tokens-panel',
  imports: [
    ReactiveFormsModule,
    LucentSelect,
    LucentIcon,
    LucentIconButton,
    LucentInlineInput,
    LucentMenu,
    LucentMenuItem,
    LucentMenuTrigger,
  ],
  template: `
    <div class="modes">
      @if (renamingMode()) {
        <lucent-inline-input
          class="mode-name"
          typography="typography-body-sm"
          [formControl]="modeNameControl"
          (focusout)="commitModeName()"
          (keydown.enter)="blur($event)"
          (keydown.escape)="cancelModeName($event)"
        />
      } @else {
        <lucent-select class="mode-select" size="sm" [options]="modeOptions()" [formControl]="modeControl" />
      }
      <lucent-icon-button icon="add" size="sm" variant="ghost" ariaLabel="Add mode" (clicked)="addMode.emit()" />
      <lucent-icon-button
        icon="more_vert"
        size="sm"
        variant="ghost"
        ariaLabel="Mode options"
        [lucentMenuTrigger]="modeMenu"
      />
      <ng-template #modeMenu>
        <lucent-menu>
          <lucent-menu-item label="Rename mode" icon="edit" (click)="startModeRename()" />
          @if (!activeModeRow()?.isDefault) {
            <lucent-menu-item label="Delete mode" icon="delete" variant="danger" (click)="deleteMode.emit(activeMode())" />
          }
        </lucent-menu>
      </ng-template>
    </div>

    <div class="families">
      @for (family of families(); track family.type) {
        <section class="family">
          <header class="family-header">
            <h3 class="family-title typography-caption">{{ family.label }}</h3>
            <lucent-icon-button
              icon="add"
              size="sm"
              variant="ghost"
              [ariaLabel]="'Add ' + family.label.toLowerCase() + ' token'"
              (clicked)="addToken.emit(family.type)"
            />
          </header>
          @for (token of family.tokens; track token.id) {
            <button
              type="button"
              class="token typography-body-sm"
              [class.selected]="token.id === selectedId()"
              [class.deprecated]="token.deprecated"
              (click)="activate.emit(token.id)"
            >
              @if (family.type === 'color') {
                <span class="swatch" [style.background]="swatch(token)"></span>
              } @else {
                <span class="value">{{ magnitude(token) }}</span>
              }
              <span class="name">{{ token.identifier }}</span>
              @if (token.source) {
                <lucent-icon class="alias" name="link" size="xs" ariaLabel="Alias" />
              }
            </button>
          }
        </section>
      }
    </div>
  `,
  styleUrls: ['./tokens-panel.scss', './token-row.scss'],
})
export class TokensPanel {
  readonly tokens = input.required<readonly TokenRow[]>();
  readonly modes = input.required<readonly ModeRow[]>();
  readonly activeMode = input.required<string>();
  readonly selectedId = input<string | null>(null);

  readonly activate = output<string>();
  readonly addToken = output<TokenType>();
  readonly modeChange = output<string>();
  readonly addMode = output<void>();
  readonly renameMode = output<ModeRename>();
  readonly deleteMode = output<string>();

  protected readonly families = computed(() =>
    TOKEN_FAMILIES.map((family) => ({ ...family, tokens: this.tokens().filter((t) => t.type === family.type) })),
  );
  protected readonly modeOptions = computed(() => this.modes().map((mode) => ({ label: mode.name, value: mode.id })));
  protected readonly activeModeRow = computed(() => this.modes().find((mode) => mode.id === this.activeMode()));

  protected readonly modeControl = new FormControl('', { nonNullable: true });
  protected readonly modeNameControl = new FormControl('', { nonNullable: true });
  protected readonly renamingMode = signal(false);

  constructor() {
    effect(() => this.modeControl.setValue(this.activeMode(), { emitEvent: false }));
    this.modeControl.valueChanges.pipe(takeUntilDestroyed()).subscribe((id) => this.modeChange.emit(id));
  }

  protected swatch(token: TokenRow): string {
    return token.resolved && 'hex' in token.resolved ? token.resolved.hex : 'transparent';
  }

  protected magnitude(token: TokenRow): string {
    return token.resolved && 'value' in token.resolved ? formatMagnitude(token.resolved) : '';
  }

  protected startModeRename(): void {
    this.modeNameControl.setValue(this.activeModeRow()?.name ?? '');
    this.renamingMode.set(true);
    setTimeout(() => document.querySelector<HTMLInputElement>('app-tokens-panel .mode-name input')?.select());
  }

  protected commitModeName(): void {
    if (!this.renamingMode()) return;
    this.renamingMode.set(false);
    const name = this.modeNameControl.value.trim();
    if (name && name !== this.activeModeRow()?.name) this.renameMode.emit({ id: this.activeMode(), name });
  }

  protected cancelModeName(event: Event): void {
    this.renamingMode.set(false);
    this.blur(event);
  }

  protected blur(event: Event): void {
    (event.target as HTMLElement).blur();
  }
}
