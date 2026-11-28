import {
  Component,
  ElementRef,
  computed,
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
    <textarea
      #field
      aria-label="Canvas text content"
      wrap="off"
      rows="1"
      [readOnly]="node().textBound"
      [style.font-size.px]="(node().text?.fontSize ?? 16) * zoom()"
      [style.line-height]="node().text?.lineHeight ?? 1.4"
      [style.letter-spacing.px]="(node().text?.letterSpacing ?? 0) * zoom()"
      [style.text-align]="node().text?.align ?? 'left'"
      [style.color]="color()"
      [style.font-weight]="weight()"
      [style.font-variation-settings]="variations()"
      [style.font-feature-settings]="features()"
      [ngModel]="draft()"
      (ngModelChange)="draft.set($event)"
      (keydown)="key($event)"
      (blur)="commit()"
    ></textarea>
    @if (node().textBound) {
      <p class="note">Content is controlled by a declared property. Edit it in the inspector.</p>
    }
    @if (error()) {
      <p class="note" role="alert">{{ error() }}</p>
    }
  `,
  styles: `
    :host {
      position: absolute;
      z-index: 5;
      display: block;
    }
    textarea {
      display: block;
      field-sizing: content;
      min-width: 100%;
      margin: 0;
      padding: 0;
      border: 0;
      outline: 1px solid #8d67e8;
      outline-offset: 2px;
      background: transparent;
      resize: none;
      overflow: hidden;
      font-family: CanvasInter, sans-serif;
    }
    .note {
      position: absolute;
      top: 100%;
      left: 0;
      margin: 8px 0 0;
      padding: 4px 8px;
      width: max-content;
      max-width: 280px;
      border-radius: 4px;
      font-size: 12px;
      background: var(--color--semantic--surface-default, #202020);
      color: var(--color--semantic--text-default, #fff);
    }
    [role='alert'] {
      color: #ffbd83;
    }
  `,
})
export class InlineTextEditor {
  readonly node = input.required<NodeProps>();
  readonly save = input.required<(id: string, text: TextRun) => boolean>();
  readonly zoom = input(1);
  readonly closed = output<void>();
  readonly weight = computed(() => this.node().text?.axes?.['wght'] ?? 400);
  readonly variations = computed(
    () => `'wght' ${this.weight()}, 'slnt' ${this.node().text?.axes?.['slnt'] ?? 0}`,
  );
  readonly features = computed(
    () =>
      Object.entries(this.node().text?.features ?? {})
        .map(([tag, value]) => `'${tag}' ${value === true ? 1 : value === false ? 0 : value}`)
        .join(', ') || null,
  );
  protected readonly color = computed(() => {
    const fill = this.node().fill as { hex?: string } | undefined;
    return fill?.hex ?? 'inherit';
  });
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
      !/^[^\u0000-\u0009\u000b-\u001f\u007f-\u009f]*$/.test(content) ||
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
