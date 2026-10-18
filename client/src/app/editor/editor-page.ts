import {
  Component,
  DestroyRef,
  afterNextRender,
  afterRenderEffect,
  computed,
  effect,
  inject,
  input,
  resource,
  signal,
} from '@angular/core';
import { Title } from '@angular/platform-browser';
import { RouterLink } from '@angular/router';
import {
  FrameLabel,
  FrameStats,
  Layer,
  ModeRow,
  NodePatch,
  NodeProps,
  OklchColor,
  TokenDef,
  TokenRow,
  TokenType,
  VisualCore,
} from '@canvas/bridge';
import { LucentButton, LucentTab, LucentTabList } from '@design-foundry/lucent';
import { Api } from '../api/api';
import { getDocument, renameDocument } from '../api/functions';
import { DocumentSync } from './document-sync';
import { ComponentCommand, EditorTool, LayerKind, LayerRow } from './editor-model';
import { EditorToolbar } from './editor-toolbar';
import { InspectorPanel } from './inspector-panel';
import { LayersPanel } from './layers-panel';
import { TokenInspector } from './token-inspector';
import { ModeRename, TokensPanel } from './tokens-panel';

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
  component: 'component',
  instance: 'instance',
};

@Component({
  selector: 'app-editor-page',
  imports: [
    RouterLink,
    LucentButton,
    LucentTabList,
    LucentTab,
    EditorToolbar,
    LayersPanel,
    TokensPanel,
    InspectorPanel,
    TokenInspector,
  ],
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
  protected readonly selectedNode = signal<NodeProps | null>(null);
  protected readonly frameLabels = signal<readonly FrameLabel[]>([]);
  protected readonly sideTab = signal<'layers' | 'tokens'>('layers');
  protected readonly tokens = signal<readonly TokenRow[]>([]);
  protected readonly modes = signal<readonly ModeRow[]>([]);
  protected readonly activeMode = signal('');
  protected readonly selectedTokenId = signal<string | null>(null);
  protected readonly selectedToken = computed(() =>
    this.sideTab() === 'tokens' ? (this.tokens().find((t) => t.id === this.selectedTokenId()) ?? null) : null,
  );
  protected readonly stats = signal('Loading visualcore');
  protected readonly syncFailing = signal(false);
  protected readonly status = computed(() =>
    this.syncFailing() ? `Changes not saved, retrying. ${this.stats()}` : this.stats(),
  );

  protected readonly core = signal<VisualCore | undefined>(undefined);
  private destroyed = false;
  private labelSizesKey = '';

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
    afterRenderEffect(() => {
      this.frameLabels();
      const core = this.core();
      if (!core) return;
      const sizes = [...document.querySelectorAll<HTMLElement>('.frame-label')].map((el) => ({
        id: el.dataset['id'] ?? '',
        w: el.offsetWidth,
        h: el.offsetHeight,
      }));
      const key = sizes.map((s) => `${s.id}:${s.w}:${s.h}`).join(';');
      if (key !== this.labelSizesKey) {
        this.labelSizesKey = key;
        core.setFrameLabelSizes(sizes);
      }
    });
  }

  protected async rename(name: string): Promise<void> {
    const renamed = await this.api.invoke(renameDocument, { id: this.id(), body: { name } });
    this.document.update((document) => document && { ...document, name: renamed.name, updatedAt: renamed.updatedAt });
  }

  protected select(id: string): void {
    this.core()?.select(id);
  }

  protected setProps(id: string, patch: NodePatch): void {
    this.core()?.setProps(id, patch);
  }

  protected setSelectedProps(patch: NodePatch): void {
    const node = this.selectedNode();
    if (node) this.setProps(node.id, patch);
  }

  protected previewSelectedProps(patch: NodePatch): void {
    const node = this.selectedNode();
    if (node) this.core()?.previewProps(node.id, patch);
  }

  protected commitPreview(): void {
    this.core()?.commitPreview();
  }

  protected runComponentCommand(command: ComponentCommand): void {
    const core = this.core();
    const node = this.selectedNode();
    if (!core || !node) return;
    switch (command) {
      case 'createInstance':
        core.createInstance(node.id);
        break;
      case 'goToMain': {
        const canvas = document.querySelector<HTMLCanvasElement>('#canvas');
        if (node.component && canvas) core.reveal(node.component, canvas.clientWidth, canvas.clientHeight);
        break;
      }
      case 'detach':
        core.detachInstance(node.id);
        break;
      case 'resetOverrides':
        core.resetOverrides(node.id);
        break;
    }
  }

  protected addToken(type: TokenType): void {
    const id = this.core()?.addToken(type);
    if (id) this.selectedTokenId.set(id);
  }

  protected readonly replaceToken = (token: TokenDef): boolean => this.core()?.replaceToken(token) ?? false;

  protected deleteToken(id: string): void {
    if (this.core()?.deleteToken(id)) this.selectedTokenId.set(null);
  }

  protected addMode(): void {
    const core = this.core();
    const id = core?.addMode();
    if (core && id) core.activeMode = id;
  }

  protected renameMode({ id, name }: ModeRename): void {
    const mode = this.modes().find((m) => m.id === id);
    if (mode) this.core()?.replaceMode({ id, name, order: mode.order });
  }

  protected setActiveMode(id: string): void {
    const core = this.core();
    if (core) core.activeMode = id;
  }

  protected readonly colorToHex = (color: OklchColor): string => this.core()?.colorToHex(color) ?? '#000000';

  protected readonly colorFromHex = (hex: string, alpha: number): OklchColor | null =>
    this.core()?.hexToColor(hex, alpha) ?? null;

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
    } else if (command && event.altKey && key === 'k') {
      core.createComponent();
    } else if (command && event.altKey && key === 'b') {
      const node = this.selectedNode();
      if (node?.type === 'instance') core.detachInstance(node.id);
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
      let structure = -1;
      let tokenRevision = -1;
      let mode = '';
      let frame = 0;
      const tick = () => {
        const labels = core.frameLabels();
        if (labels) this.frameLabels.set(labels);
        const nextTokens = core.tokenRevision();
        const nextMode = core.activeMode;
        if (nextTokens !== tokenRevision || nextMode !== mode) {
          tokenRevision = nextTokens;
          mode = nextMode;
          this.tokens.set(core.tokens());
          this.modes.set(core.modes());
          this.activeMode.set(nextMode);
        }
        const nextStructure = core.structureRevision();
        if (nextStructure !== structure) {
          structure = nextStructure;
          this.layers.set(core.layers().map(toRow));
        }
        const next = core.revision();
        if (next !== revision) {
          revision = next;
          const selection = core.selection();
          this.selectedIds.set(new Set(selection));
          this.selectedNode.set(selection.length === 1 ? core.node(selection[0]) : null);
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
  return {
    id: layer.id,
    name: layer.name,
    kind: LAYER_KINDS[layer.type],
    depth: layer.depth,
    visible: layer.visible,
    locked: layer.locked,
  };
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
