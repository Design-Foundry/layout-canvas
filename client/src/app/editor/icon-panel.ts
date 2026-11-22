import { Component, computed, effect, inject, input, signal } from '@angular/core';
import { MATERIAL_SYMBOLS_SET_ID } from '@canvas/bridge';
import type { IconRef, NodePatch, NodeProps } from '@canvas/bridge';
import { ICON_STYLES, ICON_WEIGHTS, IconService } from './icon-service';

const WEIGHT_NAMES = new Map<number, string>([
  [100, 'Thin'],
  [200, 'Extra Light'],
  [300, 'Light'],
  [400, 'Regular'],
  [500, 'Medium'],
  [600, 'Semi Bold'],
  [700, 'Bold'],
]);
const SEARCH_DELAY_MS = 250;

@Component({
  selector: 'app-icon-panel',
  template: `
    <section aria-label="Icon" class="icon-section">
      <h3 class="typography-caption">Icon</h3>
      <div class="glyph-picker typography-caption">
        <label
          >Glyph
          <input
            type="text"
            role="combobox"
            aria-label="Glyph"
            autocomplete="off"
            [attr.aria-expanded]="query() !== null"
            [value]="query() ?? glyph()"
            (focus)="openPicker()"
            (input)="typed($any($event.target).value)"
            (keydown.enter)="enter()"
            (blur)="closePicker()"
          />
        </label>
        @if (query() !== null) {
          <ul role="listbox" aria-label="Glyphs" (mousedown)="$event.preventDefault()">
            @for (option of glyphs(); track option) {
              <li role="option">
                <button type="button" (click)="pick(option)">{{ option }}</button>
              </li>
            }
          </ul>
        }
      </div>
      <label class="typography-caption"
        >Style
        <select aria-label="Style" (change)="styleChanged($event)">
          @for (style of styles; track style) {
            <option [value]="style" [selected]="style === currentStyle()">{{ style }}</option>
          }
        </select>
      </label>
      <label class="check typography-caption"
        ><input
          type="checkbox"
          aria-label="Filled"
          [checked]="filled()"
          (change)="filledChanged($event)"
        />Filled</label
      >
      <label class="typography-caption"
        >Weight
        <select aria-label="Weight" (change)="weightChanged($event)">
          @for (weight of weights; track weight) {
            <option [value]="weight" [selected]="weight === currentWeight()">
              {{ weight }} {{ weightName(weight) }}
            </option>
          }
        </select>
      </label>
      @if (error()) {
        <p role="alert" class="error typography-caption">{{ error() }}</p>
      }
    </section>
  `,
  styles: `
    :host {
      display: block;
    }
    .icon-section {
      display: grid;
      gap: 10px;
      padding: 12px;
      border-bottom: 1px solid #333;
    }
    h3,
    p {
      margin: 0;
    }
    label {
      display: grid;
      gap: 6px;
    }
    .glyph-picker {
      display: grid;
      gap: 6px;
    }
    .check {
      display: flex;
      align-items: center;
      gap: 8px;
    }
    .check input {
      margin: 0;
    }
    input[type='text'],
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
    option {
      background: #202020;
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
    .error {
      color: #ffbd83;
    }
  `,
})
export class IconPanel {
  readonly node = input.required<NodeProps>();
  readonly save = input.required<(id: string, patch: NodePatch) => boolean>();

  private readonly icons = inject(IconService);
  private searchTimer: ReturnType<typeof setTimeout> | undefined;

  protected readonly styles = ICON_STYLES;
  protected readonly weights = ICON_WEIGHTS;
  protected readonly query = signal<string | null>(null);
  protected readonly glyphs = signal<readonly string[]>([]);
  protected readonly error = signal('');

  private readonly icon = computed<IconRef>(
    () => this.node().icon ?? { set: MATERIAL_SYMBOLS_SET_ID, glyph: '' },
  );
  protected readonly glyph = computed(() => this.icon().glyph);
  protected readonly currentStyle = computed(() => String(this.icon().axes?.['style'] ?? 'outlined'));
  protected readonly filled = computed(() => Number(this.icon().axes?.['FILL'] ?? 0) === 1);
  protected readonly currentWeight = computed(() => Number(this.icon().axes?.['wght'] ?? 400));

  constructor() {
    effect(() => {
      this.node();
      this.error.set('');
    });
  }

  protected weightName(weight: number): string {
    return WEIGHT_NAMES.get(weight) ?? '';
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
    this.searchTimer = setTimeout(() => this.search(value), SEARCH_DELAY_MS);
  }

  protected enter(): void {
    const typed = (this.query() ?? '').trim().toLowerCase().replace(/[\s-]+/g, '_');
    const options = this.glyphs();
    const match = options.includes(typed) ? typed : options[0];
    if (match) this.pick(match);
  }

  protected pick(glyph: string): void {
    this.closePicker();
    this.change({ glyph });
  }

  protected styleChanged(event: Event): void {
    const select = event.target as HTMLSelectElement;
    if (!this.change({ axes: { ...this.icon().axes, style: select.value } })) select.value = this.currentStyle();
  }

  protected filledChanged(event: Event): void {
    const box = event.target as HTMLInputElement;
    if (!this.change({ axes: { ...this.icon().axes, FILL: box.checked ? 1 : 0 } })) box.checked = this.filled();
  }

  protected weightChanged(event: Event): void {
    const select = event.target as HTMLSelectElement;
    if (!this.change({ axes: { ...this.icon().axes, wght: Number(select.value) } }))
      select.value = String(this.currentWeight());
  }

  private search(query: string): void {
    this.icons
      .search(query.trim())
      .then((found) => {
        if (this.query() !== null) this.glyphs.set(found.glyphs);
      })
      .catch((error) => console.warn('Icon search failed', error));
  }

  private change(patch: Partial<IconRef>): boolean {
    const icon: IconRef = { ...this.icon(), ...patch };
    const ok = this.save()(this.node().id, { icon });
    this.error.set(ok ? '' : 'The icon change was refused. The saved layer was kept.');
    return ok;
  }
}
