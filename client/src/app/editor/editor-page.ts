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
import { FrameStats, VisualCore } from '@canvas/bridge';
import { LucentButton } from '@design-foundry/lucent';
import { Api } from '../api/api';
import { getDocument, renameDocument } from '../api/functions';
import { EditorTool, LayerRow } from './editor-model';
import { EditorToolbar } from './editor-toolbar';
import { InspectorPanel } from './inspector-panel';
import { LayersPanel } from './layers-panel';

@Component({
  selector: 'app-editor-page',
  imports: [RouterLink, LucentButton, EditorToolbar, LayersPanel, InspectorPanel],
  templateUrl: './editor-page.html',
  styleUrl: './editor-page.scss',
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
  protected readonly status = signal('loading visualcore…');

  private destroyed = false;

  constructor() {
    this.destroyRef.onDestroy(() => (this.destroyed = true));
    effect(() => {
      const name = this.name();
      this.title.setTitle(name ? `${name} - Canvas` : 'Canvas');
    });
    afterNextRender(() => void this.start());
  }

  protected async rename(name: string): Promise<void> {
    const renamed = await this.api.invoke(renameDocument, { id: this.id(), body: { name } });
    this.document.update((document) => document && { ...document, name: renamed.name, updatedAt: renamed.updatedAt });
  }

  protected select(id: string): void {
    this.selectedIds.set(new Set([id]));
  }

  private async start(): Promise<void> {
    try {
      const core = await VisualCore.load({ wasmUrl: 'wasm/visualcore.wasm' });
      if (this.destroyed) return;

      core.attach('#canvas');
      core.addRandomShapes(10_000, 1);

      const timer = setInterval(() => this.status.set(formatStats(core.frameStats())), STATS_INTERVAL_MS);
      this.destroyRef.onDestroy(() => {
        clearInterval(timer);
        core.detach();
      });
    } catch (err) {
      this.status.set(`visualcore failed to load: ${err}`);
      console.error(err);
    }
  }
}

const STATS_INTERVAL_MS = 500;

function formatStats({ frameMs, cpuMs, visible, total }: FrameStats): string {
  return `${frameMs.toFixed(1)} ms frame, ${cpuMs.toFixed(2)} ms cpu, ${visible} of ${total} shapes drawn`;
}
