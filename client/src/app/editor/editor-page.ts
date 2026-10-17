import {
  Component,
  DestroyRef,
  afterNextRender,
  computed,
  effect,
  inject,
  input,
  resource,
  signal,
} from '@angular/core';
import { Title } from '@angular/platform-browser';
import { RouterLink } from '@angular/router';
import { FrameStats, Layer, VisualCore } from '@canvas/bridge';
import { LucentButton } from '@design-foundry/lucent';
import { Api } from '../api/api';
import { getDocument, renameDocument } from '../api/functions';
import { DocumentSync } from './document-sync';
import { EditorTool, LayerKind, LayerRow } from './editor-model';
import { EditorToolbar } from './editor-toolbar';
import { InspectorPanel } from './inspector-panel';
import { LayersPanel } from './layers-panel';

const STATS_INTERVAL_MS = 500;

const TOOL_SHORTCUTS: Readonly<Record<string, EditorTool>> = {
  v: 'select',
  f: 'frame',
  r: 'rectangle',
  o: 'ellipse',
  h: 'hand',
};

const NUDGE = 1;
const BIG_NUDGE = 10;

const NUDGE_DIRECTIONS: Readonly<Record<string, readonly [number, number]>> = {
  arrowleft: [-1, 0],
  arrowright: [1, 0],
  arrowup: [0, -1],
  arrowdown: [0, 1],
};

const LAYER_KINDS: Readonly<Record<Layer['type'], LayerKind>> = {
  frame: 'frame',
  rect: 'rectangle',
  ellipse: 'ellipse',
  instance: 'instance',
};

@Component({
  selector: 'app-editor-page',
  imports: [RouterLink, LucentButton, EditorToolbar, LayersPanel, InspectorPanel],
  templateUrl: './editor-page.html',
  styleUrl: './editor-page.scss',
  host: { '(document:keydown)': 'onKeydown($event)' },
})
export class EditorPage {
  readonly id = input.required<string>();

  private readonly api = inject(Api);
  private readonly title = inject(Title);
  private readonly destroyRef = inject(DestroyRef);

  protected readonly document = resource({
    params: () => ({ id: this.id() }),
    loader: ({ params }) => this.api.invoke(getDocument, params),
  });

  protected readonly name = computed(() => (this.document.hasValue() ? this.document.value().name : ''));

  protected readonly tool = signal<EditorTool>('select');
  protected readonly layers = signal<readonly LayerRow[]>([]);
  protected readonly selectedIds = signal<ReadonlySet<string>>(new Set());
  protected readonly stats = signal('Loading visualcore');
  protected readonly syncFailing = signal(false);
  protected readonly status = computed(() =>
    this.syncFailing() ? `Changes not saved, retrying. ${this.stats()}` : this.stats(),
  );

  private readonly core = signal<VisualCore | undefined>(undefined);
  private destroyed = false;

  constructor() {
    this.destroyRef.onDestroy(() => (this.destroyed = true));
    effect(() => {
      const name = this.name();
      this.title.setTitle(name ? `${name} - Canvas` : 'Canvas');
    });
    effect(() => {
      const core = this.core();
      if (core) core.tool = this.tool();
    });
    afterNextRender(() => void this.start());
  }

  protected async rename(name: string): Promise<void> {
    const renamed = await this.api.invoke(renameDocument, { id: this.id(), body: { name } });
    this.document.update((document) => document && { ...document, name: renamed.name, updatedAt: renamed.updatedAt });
  }

  protected select(id: string): void {
    this.core()?.select(id);
  }

  protected onKeydown(event: KeyboardEvent): void {
    const core = this.core();
    if (!core || event.defaultPrevented || isTyping(event.target)) return;

    const key = event.key.toLowerCase();
    const command = event.ctrlKey || event.metaKey;
    let handled = true;
    if (command && key === 'z') {
      if (event.shiftKey) core.redo();
      else core.undo();
    } else if (command && key === 'y') {
      core.redo();
    } else if (!command && (key === 'delete' || key === 'backspace')) {
      core.deleteSelection();
    } else if (!command && NUDGE_DIRECTIONS[key]) {
      const [dx, dy] = NUDGE_DIRECTIONS[key];
      const step = event.shiftKey ? BIG_NUDGE : NUDGE;
      core.nudge(dx * step, dy * step);
    } else if (!command && !event.altKey && TOOL_SHORTCUTS[key]) {
      this.tool.set(TOOL_SHORTCUTS[key]);
    } else {
      handled = false;
    }
    if (handled) event.preventDefault();
  }

  private async start(): Promise<void> {
    try {
      const core = await VisualCore.load({ baseUrl: 'wasm/' });
      if (this.destroyed) return;

      core.attach('#canvas');
      const sync = new DocumentSync(this.api, core, this.id(), (failing) => this.syncFailing.set(failing));
      await sync.open();
      if (this.destroyed) {
        core.detach();
        return;
      }
      this.core.set(core);

      let revision = -1;
      let frame = 0;
      const tick = () => {
        const next = core.revision();
        if (next !== revision) {
          revision = next;
          this.layers.set(core.layers().map(toRow));
          this.selectedIds.set(new Set(core.selection()));
          this.tool.set(core.tool);
        }
        frame = requestAnimationFrame(tick);
      };
      frame = requestAnimationFrame(tick);
      const timer = setInterval(() => this.stats.set(formatStats(core.frameStats())), STATS_INTERVAL_MS);

      this.destroyRef.onDestroy(() => {
        cancelAnimationFrame(frame);
        clearInterval(timer);
        void sync.close();
        core.detach();
      });
    } catch (err) {
      this.stats.set(`Couldn't open the document: ${err}`);
      console.error(err);
    }
  }
}

function toRow(layer: Layer): LayerRow {
  return { id: layer.id, name: layer.name, kind: LAYER_KINDS[layer.type], depth: layer.depth };
}

function isTyping(target: EventTarget | null): boolean {
  return (
    target instanceof HTMLElement &&
    (target.isContentEditable || target instanceof HTMLInputElement || target instanceof HTMLTextAreaElement)
  );
}

function formatStats({ frameMs, cpuMs, visible, total }: FrameStats): string {
  return `${frameMs.toFixed(1)} ms frame, ${cpuMs.toFixed(2)} ms cpu, ${visible} of ${total} shapes drawn`;
}
