import { Component, computed, effect, inject, input, signal } from '@angular/core';
import { FormsModule, type NgModel } from '@angular/forms';
import { INTER_STACK_ID } from '@canvas/bridge';
import type { Font, NodeProps, TextRun, NodePatch, TokenRow } from '@canvas/bridge';
import { LucentIcon, LucentTooltip } from '@designfoundry/lucent-angular';
import { FontService } from './font-service';
import { NumberField } from './number-field';

const INITIAL: TextRun = {
  content: '',
  stack: INTER_STACK_ID,
  fontSize: 16,
  lineHeight: 1.4,
  letterSpacing: 0,
  align: 'left',
};
const WEIGHT_NAMES = new Map([
  [100, 'Thin'],
  [200, 'Extra Light'],
  [300, 'Light'],
  [400, 'Regular'],
  [500, 'Medium'],
  [600, 'Semi Bold'],
  [700, 'Bold'],
  [800, 'Extra Bold'],
  [900, 'Black'],
]);
const AXIS_NAMES = new Map([
  ['wdth', 'Width'],
  ['opsz', 'Optical size'],
]);
const FEATURE_NAMES = new Map([
  ['tnum', 'Tabular figures'],
  ['lnum', 'Lining figures'],
  ['onum', 'Oldstyle figures'],
  ['pnum', 'Proportional figures'],
  ['liga', 'Ligatures'],
  ['dlig', 'Discretionary ligatures'],
  ['calt', 'Contextual alternates'],
  ['smcp', 'Small caps'],
  ['c2sc', 'Capitals to small caps'],
  ['case', 'Case-sensitive forms'],
  ['frac', 'Fractions'],
  ['zero', 'Slashed zero'],
  ['kern', 'Kerning'],
]);
const DEFAULT_ON_FEATURES = new Set(['kern', 'liga', 'calt', 'clig', 'rlig', 'locl', 'ccmp', 'mark', 'mkmk']);
type Axis = Font['capabilities']['axes'][number];

function featureName(tag: string): string {
  const named = FEATURE_NAMES.get(tag);
  if (named) return named;
  const set = /^ss(\d\d)$/.exec(tag);
  if (set && +set[1] >= 1 && +set[1] <= 20) return `Stylistic set ${+set[1]}`;
  const variant = /^cv(\d\d)$/.exec(tag);
  if (variant && +variant[1] >= 1) return `Character variant ${+variant[1]}`;
  return tag;
}
function weightLabel(value: number): string {
  const name = WEIGHT_NAMES.get(value);
  return name ? `${value} ${name}` : String(value);
}
function allows(axis: Axis, value: number): boolean {
  const { min, max, values } = axis.domain;
  if (values) return values.includes(value);
  return (min === undefined || value >= min) && (max === undefined || value <= max);
}
const ALLOWED = /^[^\u0000-\u0009\u000b-\u001f\u007f-\u009f]*$/;

@Component({
  selector: 'app-text-panel',
  imports: [FormsModule, NumberField, LucentIcon, LucentTooltip],
  template: `
    <section aria-label="Text content" class="text-section">
      <h3 class="typography-caption">{{ styleOnly() ? 'Typography' : 'Text' }}</h3>
      @if (!styleOnly()) {
        <label class="typography-caption"
          >Content
          <textarea
            aria-label="Text content"
            [readOnly]="node().textBound"
            [ngModel]="content()"
            (ngModelChange)="content.set($event)"
            (blur)="commitContent()"
            maxlength="4096"
            rows="3"
          ></textarea>
        </label>
        <label class="typography-caption"
          >Shared typography
          <select
            aria-label="Shared typography"
            [disabled]="!!node().inInstance"
            [ngModel]="node().typography ?? ''"
            #sharedControl="ngModel"
            (ngModelChange)="bind($event, sharedControl)"
          >
            <option value="">Local typography</option>
            @for (token of styles(); track token.id) {
              <option [value]="token.id">{{ token.displayName || token.identifier }}</option>
            }
          </select>
        </label>
        @if (node().typography) {
          @if (!node().inInstance) {
            <p class="typography-caption">
              Edit the shared style in Tokens, or detach it to edit this layer.
            </p>
            <button type="button" (click)="detach()">Detach typography</button>
          } @else {
            <p class="typography-caption">
              Shared style inherited from the source. Changes here override only the edited fields.
            </p>
          }
        }
      }
      <div class="font-picker typography-caption">
        <label
          >Font
          <span class="font-field">
            <input
              type="text"
              role="combobox"
              aria-label="Font"
              autocomplete="off"
              [attr.aria-expanded]="query() !== null"
              [disabled]="!!node().typography && !node().inInstance"
              [value]="query() ?? stackName()"
              (focus)="openPicker()"
              (input)="typed($any($event.target).value)"
              (blur)="closePicker()"
            />
            @if (cue(); as message) {
              <lucent-icon
                class="cue"
                name="warning"
                size="sm"
                [ariaLabel]="message"
                [lucentTooltip]="message"
              />
            }
          </span>
        </label>
        @if (query() !== null) {
          <ul role="listbox" aria-label="Fonts" (mousedown)="$event.preventDefault()">
            @for (option of options(); track option) {
              <li role="option">
                <button type="button" (click)="pick(option)">{{ option }}</button>
              </li>
            }
          </ul>
        }
      </div>
      @if (font(); as face) {
        @if (weights().length) {
          <label class="typography-caption"
            >Weight
            <select aria-label="Weight" [disabled]="locked()" (change)="weightChanged($event)">
              @for (weight of weights(); track weight) {
                <option [value]="weight" [selected]="weight === currentWeight()">
                  {{ weightLabel(weight) }}
                </option>
              }
            </select>
          </label>
        }
        @if (italicAxis()) {
          <label class="check typography-caption"
            ><input
              type="checkbox"
              aria-label="Italic"
              [disabled]="locked()"
              [checked]="italic()"
              (change)="italicChanged($event)"
            />Italic</label
          >
        }
        @for (axis of otherAxes(); track axis.tag) {
          @if (axis.domain.values; as values) {
            <label class="typography-caption"
              >{{ axisLabel(axis.tag) }}
              <select
                [attr.aria-label]="axisLabel(axis.tag)"
                [disabled]="locked()"
                (change)="axisSelected(axis, $event)"
              >
                @for (option of values; track option) {
                  <option [value]="option" [selected]="option === axisValue(axis)">{{ option }}</option>
                }
              </select>
            </label>
          } @else {
            <app-number-field
              [label]="axisLabel(axis.tag)"
              [value]="axisValue(axis)"
              [min]="axis.domain.min ?? -999999"
              [max]="axis.domain.max ?? 999999"
              [disabled]="locked()"
              (commit)="axisCommitted(axis, $event)"
            />
          }
        }
        @if (face.capabilities.features.length) {
          <details class="features">
            <summary class="typography-caption">Features</summary>
            @for (tag of face.capabilities.features; track tag) {
              <label class="check typography-caption"
                ><input
                  type="checkbox"
                  [attr.aria-label]="featureName(tag)"
                  [disabled]="locked()"
                  [checked]="featureOn(tag)"
                  (change)="featureChanged(tag, $event)"
                />{{ featureName(tag) }}</label
              >
            }
          </details>
        }
      }
      @if (!styleOnly()) {
        <label class="wrap-toggle typography-caption"
          ><input
            type="checkbox"
            aria-label="Wrap text to width"
            #wrapControl="ngModel"
            [ngModel]="node().textWrap ?? false"
            (ngModelChange)="wrap($event, wrapControl)"
          />Wrap text to width</label
        >
        @if (node().textBound) {
          <p class="typography-caption">
            Content is controlled by a declared property. Edit it on the owning component or
            instance.
          </p>
        }
      }
      <fieldset [disabled]="!!node().typography && !node().inInstance">
        <div class="pair">
          <app-number-field
            label="Font size"
            [value]="value().fontSize"
            [min]="1"
            [max]="512"
            (commit)="number('fontSize', $event)"
          />
          <app-number-field
            label="Line height"
            [value]="value().lineHeight"
            [min]="1"
            [max]="4"
            (commit)="number('lineHeight', $event)"
          />
        </div>
        <app-number-field
          label="Letter spacing"
          [value]="value().letterSpacing"
          [min]="0"
          [max]="128"
          (commit)="number('letterSpacing', $event)"
        />
        <label class="typography-caption"
          >Alignment
          <select
            aria-label="Text alignment"
            [ngModel]="value().align"
            (ngModelChange)="change({ align: $event })"
          >
            <option value="left">Left</option>
            <option value="center">Center</option>
            <option value="right">Right</option>
          </select>
        </label>
      </fieldset>
      @if (!styleOnly()) {
        <p class="typography-caption">
          With wrapping enabled, use Fixed or Fill width and
          Hug height to grow with the lines.
        </p>
      }
      @if (error()) {
        <p role="alert" class="error typography-caption">{{ error() }}</p>
      }
    </section>
  `,
  styles: `
    fieldset {
      border: 0;
      margin: 0;
      padding: 0;
      min-width: 0;
      display: grid;
      gap: 10px;
    }
    :host {
      display: block;
    }
    .text-section {
      display: grid;
      gap: 10px;
      padding: 12px;
      border-bottom: 1px solid #333;
    }
    h3,
    p {
      margin: 0;
    }
    p {
      color: #aaa;
    }
    label {
      display: grid;
      gap: 6px;
    }
    .font-picker {
      display: grid;
      gap: 6px;
    }
    .font-field {
      display: flex;
      align-items: center;
      gap: 6px;
    }
    .cue {
      flex: none;
      color: #ffbd83;
    }
    .check {
      display: flex;
      align-items: center;
      gap: 8px;
    }
    .check input {
      margin: 0;
    }
    .features {
      display: grid;
      gap: 6px;
    }
    .features[open] {
      max-height: 220px;
      overflow-y: auto;
    }
    summary {
      cursor: pointer;
      margin-bottom: 6px;
    }
    .font-picker input {
      font: inherit;
      color: inherit;
      background: transparent;
      border: 1px solid #444;
      border-radius: 4px;
      padding: 8px;
      box-sizing: border-box;
      width: 100%;
    }
    ul {
      list-style: none;
      margin: 0;
      padding: 0;
      max-height: 220px;
      overflow-y: auto;
      border: 1px solid #444;
      border-radius: 4px;
    }
    li button {
      font: inherit;
      color: inherit;
      background: transparent;
      border: 0;
      text-align: left;
      width: 100%;
      padding: 6px 8px;
      cursor: pointer;
    }
    li button:hover,
    li button:focus-visible {
      background: #2a2a2a;
    }
    .wrap-toggle {
      display: flex;
      align-items: center;
      gap: 8px;
    }
    .wrap-toggle input {
      margin: 0;
    }
    textarea,
    select {
      font: inherit;
      color: inherit;
      background: transparent;
      border: 1px solid #444;
      border-radius: 4px;
      padding: 8px;
      box-sizing: border-box;
      width: 100%;
    }
    textarea {
      resize: vertical;
      min-height: 70px;
    }
    option {
      background: #202020;
    }
    .pair {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 8px;
    }
    .error {
      color: #ffbd83;
    }
  `,
})
export class TextPanel {
  readonly styleOnly = input(false);
  readonly tokens = input<readonly TokenRow[]>([]);
  readonly saveStyleBinding = input<(id: string, patch: NodePatch) => boolean>(() => false);
  protected readonly styles = computed(() =>
    this.tokens().filter((token) => token.type === 'typography'),
  );
  protected bind(id: string, control: NgModel): void {
    const patch: NodePatch = id ? { typography: id } : this.detachedPatch();
    const ok = this.saveStyleBinding()(this.node().id, patch);
    if (!ok)
      control.control.setValue(this.node().typography ?? '', {
        emitEvent: false,
        emitViewToModelChange: false,
      });
    this.error.set(ok ? '' : 'The typography binding was refused. The saved layer was kept.');
  }
  private detachedPatch(): NodePatch {
    const { content: _content, ...textStyle } = this.value();
    return { typography: '00000000-0000-0000-0000-000000000000', textStyle };
  }
  protected detach(): void {
    const ok = this.saveStyleBinding()(this.node().id, this.detachedPatch());
    this.error.set(ok ? '' : 'Detaching typography was refused. The saved style was kept.');
  }
  readonly node = input.required<NodeProps>();
  readonly save = input.required<(id: string, text: TextRun) => boolean>();
  readonly saveWrap = input<(id: string, wrap: boolean) => boolean>(() => false);
  protected wrap(value: boolean, control: NgModel): void {
    const ok = this.saveWrap()(this.node().id, value);
    if (!ok)
      control.control.setValue(this.node().textWrap ?? false, {
        emitEvent: false,
        emitViewToModelChange: false,
      });
    this.error.set(ok ? '' : 'The wrapping change was refused. The saved layer was kept.');
  }
  private readonly fontService = inject(FontService);
  protected readonly query = signal<string | null>(null);
  private readonly families = signal<readonly string[]>([]);
  private searchTimer: ReturnType<typeof setTimeout> | undefined;
  protected readonly locked = computed(() => !!this.node().typography && !this.node().inInstance);
  protected readonly font = computed<Font | undefined>(() => {
    const { fonts, fontStacks } = this.fontService.snapshot();
    const stack = fontStacks.find((candidate) => candidate.id === this.value().stack);
    return fonts.find((candidate) => candidate.id === stack?.entries[0]);
  });
  private readonly axes = computed(() => this.font()?.capabilities.axes ?? []);
  protected readonly italicAxis = computed<Axis | undefined>(() => {
    const axes = this.axes();
    const ital = axes.find((axis) => axis.tag === 'ital' && allows(axis, 1));
    if (ital) return ital;
    return axes.find((axis) => axis.tag === 'slnt' && (axis.domain.min ?? 0) < 0);
  });
  protected readonly otherAxes = computed(() => {
    const italic = this.italicAxis();
    return this.axes().filter((axis) => axis.tag !== 'wght' && axis !== italic);
  });
  protected readonly currentWeight = computed(() => {
    const axis = this.axes().find((candidate) => candidate.tag === 'wght');
    return this.value().axes?.['wght'] ?? axis?.default ?? 400;
  });
  protected readonly weights = computed(() => {
    const axis = this.axes().find((candidate) => candidate.tag === 'wght');
    if (!axis) return [];
    if (axis.domain.values) return [...axis.domain.values].sort((a, b) => a - b);
    const stops = [...WEIGHT_NAMES.keys()].filter((stop) => allows(axis, stop));
    const current = this.currentWeight();
    return stops.includes(current) ? stops : [...stops, current].sort((a, b) => a - b);
  });
  protected readonly italic = computed(() => {
    const axis = this.italicAxis();
    if (!axis) return false;
    const value = this.value().axes?.[axis.tag] ?? axis.default ?? 0;
    return axis.tag === 'ital' ? value === 1 : value < 0;
  });
  protected readonly cue = computed(() => {
    this.fontService.unavailable();
    this.fontService.loadedFamilies();
    this.fontService.snapshot();
    if (this.styleOnly()) return '';
    const report = this.fontService.fallbackOf(this.node().id);
    if (!report) return '';
    const { fontStacks } = this.fontService.snapshot();
    const family = fontStacks.find((stack) => stack.id === this.value().stack)?.genericFallback;
    const lines: string[] = [];
    if (report.approximated && report.generic && family && family !== 'none')
      lines.push(`Drawn with ${report.generic} in place of ${family}`);
    if (report.missing) lines.push('Some characters have no glyph');
    return lines.join('\n');
  });
  protected readonly weightLabel = weightLabel;
  protected readonly featureName = featureName;
  protected axisLabel(tag: string): string {
    return AXIS_NAMES.get(tag) ?? tag;
  }
  protected axisValue(axis: Axis): number {
    return this.value().axes?.[axis.tag] ?? axis.default ?? axis.domain.min ?? axis.domain.values?.[0] ?? 0;
  }
  protected featureOn(tag: string): boolean {
    const value = this.value().features?.[tag];
    return value === undefined ? DEFAULT_ON_FEATURES.has(tag) : value !== false && value !== 0;
  }
  private setAxis(tag: string, value: number): boolean {
    const { stack, axes, features } = this.value();
    return this.change({ stack, axes: { ...axes, [tag]: value }, features: features ?? {} });
  }
  protected weightChanged(event: Event): void {
    const select = event.target as HTMLSelectElement;
    if (!this.setAxis('wght', Number(select.value))) select.value = String(this.currentWeight());
  }
  protected italicChanged(event: Event): void {
    const box = event.target as HTMLInputElement;
    const axis = this.italicAxis();
    if (!axis) return;
    const off = allows(axis, 0) ? 0 : (axis.domain.min ?? 0);
    const on = axis.tag === 'ital' ? 1 : (axis.domain.min ?? 0);
    if (!this.setAxis(axis.tag, box.checked ? on : off)) box.checked = this.italic();
  }
  protected axisSelected(axis: Axis, event: Event): void {
    const select = event.target as HTMLSelectElement;
    if (!this.setAxis(axis.tag, Number(select.value))) select.value = String(this.axisValue(axis));
  }
  protected axisCommitted(axis: Axis, value: number | null): void {
    if (value === null) return;
    const { min, max } = axis.domain;
    this.setAxis(axis.tag, Math.min(max ?? value, Math.max(min ?? value, value)));
  }
  protected featureChanged(tag: string, event: Event): void {
    const box = event.target as HTMLInputElement;
    const { stack, axes, features = {} } = this.value();
    const next: Record<string, boolean | number> = { ...features };
    if (box.checked) next[tag] = true;
    else if (DEFAULT_ON_FEATURES.has(tag)) next[tag] = false;
    else delete next[tag];
    if (!this.change({ stack, axes: axes ?? {}, features: next })) box.checked = this.featureOn(tag);
  }
  protected readonly stackName = computed(() => {
    this.fontService.snapshot();
    return this.fontService.nameOf(this.value().stack);
  });
  protected readonly options = computed(() => {
    const query = (this.query() ?? '').trim().toLowerCase();
    const inter = !query || 'inter'.includes(query) ? ['Inter'] : [];
    return [...inter, ...this.families().filter((family) => family !== 'Inter')];
  });
  protected readonly pending = signal<TextRun | null>(null);
  protected readonly value = computed(() => this.pending() ?? this.node().text ?? INITIAL);
  protected readonly content = signal('');
  protected readonly error = signal('');

  constructor() {
    effect(() => {
      const node = this.node();
      this.pending.set(null);
      this.content.set(node.text?.content ?? '');
      this.error.set('');
    });
  }

  protected commitContent(): void {
    if (this.node().textBound) return;
    const content = this.content().normalize('NFC');
    if (
      !ALLOWED.test(content) ||
      [...content].length > 2048 ||
      new TextEncoder().encode(content).length > 4096
    ) {
      this.error.set(
        'This text contains unsupported characters or exceeds the 2048-character limit. Your saved text was kept.',
      );
      return;
    }
    this.content.set(content);
    this.error.set('');
    if (content !== this.value().content) this.change({ content });
  }

  protected number(key: 'fontSize' | 'lineHeight' | 'letterSpacing', value: number | null): void {
    if (value !== null) this.change({ [key]: value });
  }

  protected openPicker(): void {
    this.query.set('');
    this.search('');
  }

  protected closePicker(): void {
    clearTimeout(this.searchTimer);
    this.query.set(null);
  }

  protected typed(value: string): void {
    this.query.set(value);
    clearTimeout(this.searchTimer);
    this.searchTimer = setTimeout(() => this.search(value), 250);
  }

  private search(query: string): void {
    this.fontService
      .search(query.trim())
      .then((families) => {
        if (this.query() !== null) this.families.set(families);
      })
      .catch((error) => console.warn('Font search failed', error));
  }

  protected async pick(family: string): Promise<void> {
    this.closePicker();
    if (family === 'Inter') {
      this.change({ stack: INTER_STACK_ID, axes: {}, features: undefined });
      return;
    }
    try {
      const stack = await this.fontService.use(family);
      if (!stack || !this.change({ stack, axes: {}, features: undefined }))
        this.error.set('The font change was refused. The saved layer was kept.');
    } catch (error) {
      console.warn(`Font ${family} could not be added`, error);
      this.error.set('The font could not be added. The saved layer was kept.');
    }
  }

  protected change(patch: Partial<TextRun>): boolean {
    const next = { ...this.value(), ...patch };
    if (this.save()(this.node().id, next)) {
      this.pending.set(next);
      this.error.set('');
      return true;
    } else this.error.set('The text change was refused. The saved layer was kept.');
    return false;
  }
}
