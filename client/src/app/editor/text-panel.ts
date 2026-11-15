import { Component, computed, effect, input, signal } from '@angular/core';
import { FormsModule, type NgModel } from '@angular/forms';
import { INTER_STACK_ID } from '@canvas/bridge';
import type { NodeProps, TextRun, NodePatch, TokenRow } from '@canvas/bridge';
import { NumberField } from './number-field';

const INITIAL: TextRun = {
  content: '',
  stack: INTER_STACK_ID,
  fontSize: 16,
  lineHeight: 1.4,
  letterSpacing: 0,
  align: 'left',
};
const FONT_STYLES: Record<string, Record<string, number>> = {
  regular: {},
  bold: { wght: 700 },
  oblique: { slnt: -10 },
  'bold-oblique': { wght: 700, slnt: -10 },
};
const ALLOWED = /^[^\u0000-\u0009\u000b-\u001f\u007f-\u009f]*$/;

@Component({
  selector: 'app-text-panel',
  imports: [FormsModule, NumberField],
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
      <label class="typography-caption"
        >Font style<select
          aria-label="Font style"
          [disabled]="!!node().typography && !node().inInstance"
          [ngModel]="fontStyle()"
          #fontControl="ngModel"
          (ngModelChange)="fontChanged($event, fontControl)"
        >
          <option value="regular">Inter Regular</option>
          <option value="bold">Inter Bold</option>
          <option value="oblique">Inter Oblique</option>
          <option value="bold-oblique">Inter Bold Oblique</option>
        </select></label
      >
      <p class="typography-caption">Inter · included locally</p>
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

  protected fontStyle(): string {
    const { stack, axes = {}, features } = this.value();
    if (stack !== INTER_STACK_ID || (features && Object.keys(features).length)) return '';
    const key = JSON.stringify(Object.entries(axes).sort());
    return (
      Object.entries(FONT_STYLES).find(
        ([, axes]) => JSON.stringify(Object.entries(axes).sort()) === key,
      )?.[0] ?? ''
    );
  }

  protected fontChanged(style: string, control: NgModel): void {
    const previous = this.fontStyle();
    const axes = FONT_STYLES[style];
    if (!axes || !this.change({ stack: INTER_STACK_ID, axes, features: undefined }))
      control.control.setValue(previous, { emitEvent: false, emitViewToModelChange: false });
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
