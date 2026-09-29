import {
  Component,
  ElementRef,
  afterNextRender,
  computed,
  effect,
  inject,
  input,
  resource,
  signal,
  viewChild,
} from '@angular/core';
import { Title } from '@angular/platform-browser';
import { RouterLink } from '@angular/router';
import { VisualCore } from '@canvas/bridge';
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
  private readonly surface = viewChild.required<ElementRef<HTMLCanvasElement>>('surface');

  protected readonly document = resource({
    params: () => ({ id: this.id() }),
    loader: ({ params }) => this.api.invoke(getDocument, params),
  });

  protected readonly name = computed(() => (this.document.hasValue() ? this.document.value().name : ''));

  protected readonly tool = signal<EditorTool>('select');
  protected readonly layers = signal<readonly LayerRow[]>([]);
  protected readonly selectedIds = signal<ReadonlySet<string>>(new Set());
  protected readonly status = signal('loading visualcore…');

  constructor() {
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

      const camera = core.createCamera();
      try {
        camera.zoomAt({ x: 100, y: 100 }, 2);
        const { width, height } = this.surface().nativeElement.getBoundingClientRect();
        this.status.set(`visualcore ready, zoom ${camera.zoom}, surface ${width}×${height}`);
      } finally {
        camera.dispose();
      }
    } catch (err) {
      this.status.set(`visualcore failed to load: ${err}`);
      console.error(err);
    }
  }
}
