import {
  Component,
  ElementRef,
  afterNextRender,
  input,
  output,
  signal,
  viewChild,
} from '@angular/core';
import { FormsModule } from '@angular/forms';
import type { NodeProps, TextRun } from '@canvas/bridge';

@Component({
  selector: 'app-inline-text-editor',
  imports: [FormsModule],
  template: `
    <section aria-label="Edit text on canvas" (keydown)="key($event)">
      <label
        >Text
        <textarea
          #field
          aria-label="Canvas text content"
          wrap="off"
          rows="3"
          [readOnly]="node().textBound"
          [style.font-weight]="node().text?.font?.includes('bold') ? 700 : 400"
          [style.font-variation-settings]="
            node().text?.font?.includes('oblique') ? slanted : upright
          "
          [ngModel]="draft()"
          (ngModelChange)="draft.set($event)"
          (blur)="commit()"
        ></textarea>
      </label>
      @if (node().textBound) {
        <p>
          Content is controlled by a declared property. Edit it on the owning component or instance
          in the inspector.
        </p>
      } @else {
        <p>Enter: new line · Ctrl/⌘+Enter: save · Escape: cancel</p>
        <button type="button" (mousedown)="$event.preventDefault()" (click)="commit()">
          Save text
        </button>
      }
      <button type="button" (mousedown)="$event.preventDefault()" (click)="cancel()">Cancel</button>
      @if (error()) {
        <p role="alert">{{ error() }}</p>
      }
    </section>
  `,
  styles: `
    :host {
      position: absolute;
      z-index: 5;
      width: min(320px, 100%);
      max-width: 100%;
    }
    section {
      padding: 10px;
      border: 1px solid #8d67e8;
      border-radius: 6px;
      background: var(--color--semantic--surface-default, #202020);
      color: var(--color--semantic--text-default, #fff);
      box-shadow: 0 4px 20px #0005;
    }
    label {
      display: grid;
      gap: 6px;
      font-size: 12px;
    }
    textarea {
      font:
        20px/1.4 CanvasInter,
        sans-serif;
      font-variation-settings: 'slnt' 0;
      font-feature-settings:
        'calt' 0,
        'ccmp' 0,
        'locl' 0,
        'mark' 0;
      color: inherit;
      background: transparent;
      width: 100%;
      box-sizing: border-box;
      resize: vertical;
      border: 1px solid #777;
      border-radius: 4px;
    }
    p {
      font-size: 12px;
      margin: 8px 0;
    }
    button {
      color: inherit;
      background: transparent;
      border: 1px solid #777;
      border-radius: 4px;
      padding: 5px 9px;
      margin-right: 8px;
      cursor: pointer;
    }
    [role='alert'] {
      color: #ffbd83;
    }
  `,
})
export class InlineTextEditor {
  readonly slanted = "'slnt' -10";
  readonly upright = "'slnt' 0";
  readonly node = input.required<NodeProps>();
  readonly save = input.required<(id: string, text: TextRun) => boolean>();
  readonly closed = output<void>();
  protected readonly draft = signal('');
  protected readonly error = signal('');
  private readonly field = viewChild<ElementRef<HTMLTextAreaElement>>('field');
  private finished = false;
  constructor() {
    afterNextRender(() => {
      this.field()?.nativeElement.focus();
      this.field()?.nativeElement.select();
    });
  }
  ngOnInit(): void {
    this.draft.set(this.node().text?.content ?? '');
  }
  protected cancel(): void {
    this.finished = true;
    this.closed.emit();
  }
  protected key(event: KeyboardEvent): void {
    event.stopPropagation();
    if (event.isComposing) return;
    if (event.key === 'Escape') {
      event.preventDefault();
      this.cancel();
    } else if (event.key === 'Enter' && (event.ctrlKey || event.metaKey)) {
      event.preventDefault();
      this.commit();
    }
  }
  protected commit(): void {
    if (this.finished || this.node().textBound) return;
    const node = this.node(),
      content = this.draft().normalize('NFC');
    if (
      !/^[\x20-\x7e\xa0-\xff\n\u2013\u2014\u2018\u2019\u201a\u201c\u201d\u201e\u2022\u2026\u2039\u203a\u20ac\u2122]*$/.test(
        content,
      ) ||
      [...content].length > 2048 ||
      new TextEncoder().encode(content).length > 4096
    ) {
      this.error.set('Unsupported characters or text too long. The saved text was kept.');
      return;
    }
    if (
      node.text &&
      (content === node.text.content || this.save()(node.id, { ...node.text, content }))
    ) {
      this.finished = true;
      this.closed.emit();
    } else this.error.set('This text change was refused. The saved layer was kept.');
  }
}
