import { Component, computed, effect, input, output, signal } from '@angular/core';
import { takeUntilDestroyed } from '@angular/core/rxjs-interop';
import { FormControl, ReactiveFormsModule } from '@angular/forms';
import { ModeRow, OklchColor, TokenColor, TokenDef, TokenMagnitude, TokenRow, TokenUnit } from '@canvas/bridge';
import {
  LucentButton,
  LucentCheckbox,
  LucentLabel,
  LucentPrefix,
  LucentSegmentedControl,
  LucentSelect,
  LucentTextInput,
  LucentTextarea,
} from '@design-foundry/lucent';
import { NumberField } from './number-field';
import { TOKEN_FAMILIES, formatMagnitude } from './token-model';

type TokenKind = 'primitive' | 'alias';

const KIND_OPTIONS = [
  { label: 'Primitive', value: 'primitive' as TokenKind },
  { label: 'Alias', value: 'alias' as TokenKind },
];

@Component({
  selector: 'app-token-inspector',
  imports: [
    ReactiveFormsModule,
    LucentTextInput,
    LucentTextarea,
    LucentLabel,
    LucentPrefix,
    LucentCheckbox,
    LucentSegmentedControl,
    LucentSelect,
    LucentButton,
    NumberField,
  ],
  template: `
    <h2 class="title typography-label-md">{{ family().label }} token</h2>
    @if (token(); as token) {
      <section class="section">
        <lucent-text-input
          size="sm"
          [formControl]="identifierControl"
          (focusout)="commitText('identifier', identifierControl)"
          (keydown.enter)="blur($event)"
        >
          <span lucentLabel>Identifier</span>
        </lucent-text-input>
        <lucent-text-input
          size="sm"
          [formControl]="displayNameControl"
          (focusout)="commitText('displayName', displayNameControl)"
          (keydown.enter)="blur($event)"
        >
          <span lucentLabel>Display name</span>
        </lucent-text-input>
        <lucent-textarea
          size="sm"
          [rows]="2"
          [formControl]="descriptionControl"
          (focusout)="commitText('description', descriptionControl)"
        >
          <span lucentLabel>Description</span>
        </lucent-textarea>
        <lucent-checkbox size="sm" label="Deprecated" [formControl]="deprecatedControl" />
      </section>

      <section class="section">
        <lucent-segmented-control
          size="sm"
          label="Kind"
          [options]="kindOptions()"
          [value]="token.source ? 'alias' : 'primitive'"
          (valueChange)="setKind($event)"
        />

        @if (token.source; as source) {
          @for (mode of modes(); track mode.id) {
            <lucent-select
              size="sm"
              [placeholder]="mode.isDefault ? '' : 'Same as ' + defaultModeName()"
              [options]="sourceOptions(mode)"
              [formControl]="sourceControl(mode.id)"
            >
              <span lucentLabel>{{ mode.name }}</span>
            </lucent-select>
          }
        } @else if (color(); as color) {
          <div class="color">
            <label class="swatch" [style.background]="color.hex">
              <input type="color" class="picker" [value]="color.hex" (change)="pickColor($event)" />
            </label>
            <lucent-text-input
              size="sm"
              [formControl]="hexControl"
              (focusout)="commitHex()"
              (keydown.enter)="blur($event)"
            >
              <span lucentPrefix class="typography-caption">#</span>
            </lucent-text-input>
          </div>
        } @else if (magnitude(); as magnitude) {
          <div class="magnitude">
            <app-number-field
              label="Value"
              [value]="magnitude.value"
              [min]="bounds().min"
              [max]="bounds().max"
              (commit)="setMagnitude({ value: $event, unit: magnitude.unit })"
            />
            @if (family().units.length > 1) {
              <lucent-select size="sm" [options]="unitOptions()" [formControl]="unitControl" />
            }
          </div>
        }

        @if (token.source && resolved(); as resolved) {
          <div class="resolved typography-body-sm">
            @if (resolved.hex) {
              <span class="chip" [style.background]="resolved.hex"></span>
            }
            {{ resolved.text }}
          </div>
        }
      </section>

      @if (modifierLines().length) {
        <section class="section">
          <h3 class="heading typography-caption">Modifiers</h3>
          @for (line of modifierLines(); track $index) {
            <p class="modifier typography-body-sm">{{ line }}</p>
          }
        </section>
      }

      <section class="section">
        <lucent-button size="sm" variant="secondary" [disabled]="token.referenced" (clicked)="remove.emit(token.id)">
          Delete token
        </lucent-button>
      </section>
    }
  `,
  styleUrls: ['./panel.scss', './inspector-panel.scss', './token-inspector.scss'],
})
export class TokenInspector {
  readonly token = input.required<TokenRow>();
  readonly tokens = input.required<readonly TokenRow[]>();
  readonly modes = input.required<readonly ModeRow[]>();
  readonly colorFromHex = input.required<(hex: string, alpha: number) => OklchColor | null>();
  /** Replaces the token's definition. Returns false if the document refused it. */
  readonly replace = input.required<(token: TokenDef) => boolean>();
  readonly remove = output<string>();

  protected readonly family = computed(() => TOKEN_FAMILIES.find((f) => f.type === this.token().type)!);
  protected readonly siblings = computed(() =>
    this.tokens().filter((t) => t.type === this.token().type && t.id !== this.token().id),
  );
  protected readonly kindOptions = computed(() =>
    KIND_OPTIONS.map((option) => ({ ...option, disabled: option.value === 'alias' && !this.siblings().length })),
  );
  protected readonly bounds = computed(() =>
    this.token().type === 'opacity' ? { min: 0, max: 1 } : { min: -Infinity, max: Infinity },
  );
  protected readonly unitOptions = computed(() => this.family().units.map((unit) => ({ label: unit, value: unit })));
  protected readonly defaultModeName = computed(() => this.modes().find((m) => m.isDefault)?.name ?? '');
  protected readonly color = computed(() => {
    const token = this.token();
    return !token.source && token.resolved && 'hex' in token.resolved ? token.resolved : null;
  });
  protected readonly magnitude = computed(() => {
    const value = this.token().value;
    return value && 'value' in value ? value : null;
  });
  protected readonly resolved = computed(() => {
    const resolved = this.token().resolved;
    if (!resolved) return null;
    return 'hex' in resolved
      ? { hex: resolved.hex, text: resolved.a < 1 ? `${resolved.hex} ${Math.round(resolved.a * 100)}%` : resolved.hex }
      : { hex: '', text: formatMagnitude(resolved) };
  });
  protected readonly modifierLines = computed(() => (this.token().modifiers ?? []).map((m) => this.describeModifier(m)));

  protected readonly identifierControl = new FormControl('', { nonNullable: true });
  protected readonly displayNameControl = new FormControl('', { nonNullable: true });
  protected readonly descriptionControl = new FormControl('', { nonNullable: true });
  protected readonly deprecatedControl = new FormControl(false, { nonNullable: true });
  protected readonly hexControl = new FormControl('', { nonNullable: true });
  protected readonly unitControl = new FormControl<TokenUnit | null>(null);
  private readonly sourceControls = new Map<string, FormControl<string | null>>();
  private readonly syncing = signal(false);
  private shownId = '';

  constructor() {
    effect(() => {
      const token = this.token();
      const switched = token.id !== this.shownId;
      this.shownId = token.id;
      this.syncing.set(true);
      sync(this.identifierControl, token.identifier, switched);
      sync(this.displayNameControl, token.displayName ?? '', switched);
      sync(this.descriptionControl, token.description ?? '', switched);
      this.deprecatedControl.setValue(token.deprecated ?? false, { emitEvent: false });
      const color = this.color();
      if (color) sync(this.hexControl, color.hex.slice(1), switched);
      this.unitControl.setValue(this.magnitude()?.unit ?? null, { emitEvent: false });
      for (const mode of this.modes()) {
        this.sourceControl(mode.id).setValue(token.source?.[mode.id] ?? null, { emitEvent: false });
      }
      this.syncing.set(false);
    });
    this.deprecatedControl.valueChanges
      .pipe(takeUntilDestroyed())
      .subscribe((deprecated) => this.emit({ deprecated }));
    this.unitControl.valueChanges.pipe(takeUntilDestroyed()).subscribe((unit) => {
      const magnitude = this.magnitude();
      if (magnitude && unit) this.setMagnitude({ value: magnitude.value, unit });
    });
  }

  protected sourceOptions(mode: ModeRow): { label: string; value: string | null }[] {
    const options = this.siblings().map((t) => ({ label: t.identifier, value: t.id as string | null }));
    return mode.isDefault ? options : [{ label: `Same as ${this.defaultModeName()}`, value: null }, ...options];
  }

  protected sourceControl(modeId: string): FormControl<string | null> {
    let control = this.sourceControls.get(modeId);
    if (!control) {
      control = new FormControl<string | null>(null);
      control.valueChanges.subscribe((target) => this.setSource(modeId, target));
      this.sourceControls.set(modeId, control);
    }
    return control;
  }

  protected commitText(field: 'identifier' | 'displayName' | 'description', control: FormControl<string>): void {
    control.markAsPristine();
    const value = control.value.trim();
    const current = this.token()[field] ?? '';
    if (value === current) return;
    if (!this.emit({ [field]: value || undefined })) control.setValue(current);
  }

  protected commitHex(): void {
    this.hexControl.markAsPristine();
    const color = this.colorFromHex()(this.hexControl.value.trim(), 1);
    const current = this.color();
    if (!color || !current) {
      if (current) this.hexControl.setValue(current.hex.slice(1));
      return;
    }
    this.setColor(color);
  }

  protected pickColor(event: Event): void {
    const color = this.colorFromHex()((event.target as HTMLInputElement).value, 1);
    if (color) this.setColor(color);
  }

  protected setMagnitude(value: TokenMagnitude): void {
    this.emit({ value });
  }

  protected setKind(kind: TokenKind | string | null): void {
    const token = this.token();
    if (kind === 'alias' && !token.source) {
      const first = this.siblings()[0];
      if (!first) return;
      const defaultMode = this.modes().find((m) => m.isDefault)!;
      this.emit({ value: undefined, source: { [defaultMode.id]: first.id } });
    } else if (kind === 'primitive' && token.source && token.resolved) {
      const resolved = token.resolved;
      const value = 'hex' in resolved ? { l: resolved.l, c: resolved.c, h: resolved.h } : resolved;
      this.emit({ value, source: undefined });
    }
  }

  protected blur(event: Event): void {
    (event.target as HTMLElement).blur();
  }

  private setColor({ l, c, h }: TokenColor): void {
    this.emit({ value: { l, c, h } });
  }

  private setSource(modeId: string, target: string | null): void {
    if (this.syncing()) return;
    const source = { ...(this.token().source ?? {}) };
    if (target) source[modeId] = target;
    else delete source[modeId];
    this.emit({ source });
  }

  /** Replaces the token with `changes` applied. Returns false if nothing changed or it was refused. */
  private emit(changes: Partial<TokenDef>): boolean {
    const { resolved: _resolved, referenced: _referenced, ...definition } = this.token();
    const next: TokenDef = { ...definition, ...changes };
    if (JSON.stringify(next) === JSON.stringify(definition)) return false;
    return this.replace()(next);
  }

  private describeModifier(modifier: NonNullable<TokenDef['modifiers']>[number]): string {
    const nameOf = (id?: string) => this.tokens().find((t) => t.id === id)?.identifier ?? 'unknown token';
    if ('mix' in modifier) return `Mix with ${nameOf(modifier.mix)}, ${Math.round(modifier.amount * 100)}%`;
    const operand = modifier.ref ? nameOf(modifier.ref) : String(modifier.value);
    return 'channel' in modifier
      ? `${modifier.channel} ${modifier.kind} ${operand}`
      : `${modifier.kind} ${operand}`;
  }
}

/** Shows `value` unless the user is mid-edit in this field; switching tokens always wins. */
function sync(control: FormControl<string>, value: string, force: boolean): void {
  if (force || !control.dirty) {
    control.setValue(value, { emitEvent: false });
    control.markAsPristine();
  }
}
