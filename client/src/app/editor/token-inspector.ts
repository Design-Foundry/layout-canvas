import { Component, computed, effect, input, output, signal } from '@angular/core';
import { takeUntilDestroyed } from '@angular/core/rxjs-interop';
import { FormControl, ReactiveFormsModule } from '@angular/forms';
import {
  ModeRow,
  OklchColor,
  StrokeStyle,
  TokenBorder,
  TokenColor,
  TokenDef,
  TokenMagnitude,
  TokenRow,
  TokenShadowLayer,
  TokenType,
  TokenUnit,
} from '@canvas/bridge';
import {
  LucentButton,
  LucentCheckbox,
  LucentIconButton,
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

const BORDER_STYLE_OPTIONS: { label: string; value: StrokeStyle }[] = [
  { label: 'Solid', value: 'solid' },
  { label: 'Dashed', value: 'dashed' },
  { label: 'Dotted', value: 'dotted' },
  { label: 'Double', value: 'double' },
  { label: 'None', value: 'none' },
];

const SHADOW_KIND_OPTIONS = [
  { label: 'Drop', value: 'drop' },
  { label: 'Inner', value: 'inner' },
];

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
    LucentIconButton,
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

      @if (border(); as border) {
        <section class="section">
          <lucent-select size="sm" [options]="familyOptions('dimension')" [formControl]="borderWidthControl">
            <span lucentLabel>Width</span>
          </lucent-select>
          <lucent-select size="sm" [options]="borderStyleOptions" [formControl]="borderStyleControl">
            <span lucentLabel>Style</span>
          </lucent-select>
          <lucent-select size="sm" [options]="familyOptions('color')" [formControl]="borderColorControl">
            <span lucentLabel>Color</span>
          </lucent-select>
        </section>
      } @else if (shadow(); as shadow) {
        <section class="section">
          <header class="section-header">
            <h3 class="heading typography-caption">Layers</h3>
            <lucent-icon-button icon="add" size="sm" variant="ghost" ariaLabel="Add layer" (clicked)="addLayer()" />
          </header>
          @for (layer of shadow.layers; track $index; let i = $index) {
            <div class="layer">
              <div class="layer-header">
                <lucent-segmented-control
                  size="sm"
                  label="Shadow type"
                  [options]="shadowKindOptions"
                  [value]="layer.inset ? 'inner' : 'drop'"
                  (valueChange)="setLayer(i, { inset: $event === 'inner' })"
                />
                @if (shadow.layers.length > 1) {
                  <lucent-icon-button icon="remove" size="sm" variant="ghost" ariaLabel="Remove layer" (clicked)="removeLayer(i)" />
                }
              </div>
              <div class="pair">
                <app-number-field label="X" [value]="layer.offsetX" (commit)="setLayerNumber(i, 'offsetX', $event)" />
                <app-number-field label="Y" [value]="layer.offsetY" (commit)="setLayerNumber(i, 'offsetY', $event)" />
                <app-number-field label="Blur" [value]="layer.blur" [min]="0" (commit)="setLayerNumber(i, 'blur', $event)" />
                <app-number-field label="Spread" [value]="layer.spread" (commit)="setLayerNumber(i, 'spread', $event)" />
              </div>
              <lucent-select size="sm" [options]="familyOptions('color')" [formControl]="layerColorControl(i)">
                <span lucentLabel>Color</span>
              </lucent-select>
            </div>
          }
        </section>
      } @else {
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
              (commit)="$event !== null && setMagnitude({ value: $event, unit: magnitude.unit })"
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

      }

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
    if ('hex' in resolved) {
      return { hex: resolved.hex, text: resolved.a < 1 ? `${resolved.hex} ${Math.round(resolved.a * 100)}%` : resolved.hex };
    }
    return 'value' in resolved ? { hex: '', text: formatMagnitude(resolved) } : null;
  });
  protected readonly border = computed(() => {
    const value = this.token().value;
    return value && 'style' in value ? value : null;
  });
  protected readonly shadow = computed(() => {
    const value = this.token().value;
    return value && 'layers' in value ? value : null;
  });
  protected readonly borderStyleOptions = BORDER_STYLE_OPTIONS;
  protected readonly shadowKindOptions = SHADOW_KIND_OPTIONS;
  protected readonly modifierLines = computed(() => (this.token().modifiers ?? []).map((m) => this.describeModifier(m)));

  protected readonly identifierControl = new FormControl('', { nonNullable: true });
  protected readonly displayNameControl = new FormControl('', { nonNullable: true });
  protected readonly descriptionControl = new FormControl('', { nonNullable: true });
  protected readonly deprecatedControl = new FormControl(false, { nonNullable: true });
  protected readonly hexControl = new FormControl('', { nonNullable: true });
  protected readonly borderWidthControl = new FormControl<string | null>(null);
  protected readonly borderStyleControl = new FormControl<StrokeStyle | null>(null);
  protected readonly borderColorControl = new FormControl<string | null>(null);
  private readonly layerColorControls: FormControl<string | null>[] = [];
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
      const border = this.border();
      if (border) {
        this.borderWidthControl.setValue(border.width, { emitEvent: false });
        this.borderStyleControl.setValue(border.style, { emitEvent: false });
        this.borderColorControl.setValue(border.color, { emitEvent: false });
      }
      this.shadow()?.layers.forEach((layer, i) =>
        this.layerColorControl(i).setValue(layer.color, { emitEvent: false }),
      );
      for (const mode of this.modes()) {
        this.sourceControl(mode.id).setValue(token.source?.[mode.id] ?? null, { emitEvent: false });
      }
      this.syncing.set(false);
    });
    this.deprecatedControl.valueChanges
      .pipe(takeUntilDestroyed())
      .subscribe((deprecated) => this.emit({ deprecated }));
    this.borderWidthControl.valueChanges
      .pipe(takeUntilDestroyed())
      .subscribe((width) => width && this.setBorder({ width }));
    this.borderStyleControl.valueChanges
      .pipe(takeUntilDestroyed())
      .subscribe((style) => style && this.setBorder({ style }));
    this.borderColorControl.valueChanges
      .pipe(takeUntilDestroyed())
      .subscribe((color) => color && this.setBorder({ color }));
    this.unitControl.valueChanges.pipe(takeUntilDestroyed()).subscribe((unit) => {
      const magnitude = this.magnitude();
      if (magnitude && unit) this.setMagnitude({ value: magnitude.value, unit });
    });
  }

  protected familyOptions(type: TokenType): { label: string; value: string }[] {
    return this.tokens()
      .filter((t) => t.type === type)
      .map((t) => ({ label: t.identifier, value: t.id }));
  }

  protected layerColorControl(index: number): FormControl<string | null> {
    while (this.layerColorControls.length <= index) {
      const at = this.layerColorControls.length;
      const control = new FormControl<string | null>(null);
      control.valueChanges.subscribe((color) => color && !this.syncing() && this.setLayer(at, { color }));
      this.layerColorControls.push(control);
    }
    return this.layerColorControls[index];
  }

  private setBorder(changes: Partial<TokenBorder>): void {
    const border = this.border();
    if (border && !this.syncing()) this.emit({ value: { ...border, ...changes } });
  }

  protected setLayer(index: number, changes: Partial<TokenShadowLayer>): void {
    const shadow = this.shadow();
    if (!shadow) return;
    this.emit({ value: { layers: shadow.layers.map((layer, i) => (i === index ? { ...layer, ...changes } : layer)) } });
  }

  protected setLayerNumber(index: number, field: 'offsetX' | 'offsetY' | 'blur' | 'spread', value: number | null): void {
    if (value !== null) this.setLayer(index, { [field]: value });
  }

  protected addLayer(): void {
    const shadow = this.shadow();
    if (shadow) this.emit({ value: { layers: [...shadow.layers, { ...shadow.layers[shadow.layers.length - 1] }] } });
  }

  protected removeLayer(index: number): void {
    const shadow = this.shadow();
    if (shadow) this.emit({ value: { layers: shadow.layers.filter((_, i) => i !== index) } });
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
      const value = 'hex' in resolved ? { l: resolved.l, c: resolved.c, h: resolved.h } : (resolved as TokenMagnitude);
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
