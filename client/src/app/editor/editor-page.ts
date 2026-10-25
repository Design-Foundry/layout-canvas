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
  ComponentDefinition,
  ComponentValues,
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
  Rect,
  TextRun,
  RasterImage,
  PrototypeState,
  PrototypeScreen,
} from '@canvas/bridge';
import { LucentButton, LucentTab, LucentTabList } from '@designfoundry/lucent-angular';
import { Api } from '../api/api';
import { getDocument, renameDocument } from '../api/functions';
import { DocumentSync } from './document-sync';
import { watchPrototypeMotion } from './prototype-motion';
import { followTheme } from './canvas-background';
import { ComponentCommand, EditorTool, LayerKind, LayerRow } from './editor-model';
import { EditorToolbar } from './editor-toolbar';
import { InspectorPanel } from './inspector-panel';
import { LayersPanel } from './layers-panel';
import { TokenInspector } from './token-inspector';
import { ModeRename, TokensPanel } from './tokens-panel';
import { ComponentsPanel } from './components-panel';
import { componentOptions } from './component-options';
import { InlineTextEditor } from './inline-text-editor';
import { ImageImport } from './image-import';
import type { ComponentSnapshot } from './component-package';

const STATS_INTERVAL_MS = 500;

const TOOL_SHORTCUTS: Readonly<Record<string, EditorTool>> = {
  v: 'select',
  f: 'frame',
  r: 'rectangle',
  o: 'ellipse',
  l: 'line',
  p: 'pen',
  y: 'polygon',
  t: 'text',
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
  line: 'line',
  path: 'path',
  image: 'image',
  frame: 'frame',
  rect: 'rectangle',
  ellipse: 'ellipse',
  component: 'component',
  instance: 'instance',
  text: 'text',
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
    ComponentsPanel,
    InlineTextEditor,
    ImageImport,
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

  protected readonly name = computed(() =>
    this.document.hasValue() ? this.document.value().name : '',
  );

  protected readonly tool = signal<EditorTool>('select');
  protected readonly layers = signal<readonly LayerRow[]>([]);
  protected readonly selectedIds = signal<ReadonlySet<string>>(new Set());
  protected readonly selectedNode = signal<NodeProps | null>(null);
  protected readonly presentation = signal<PrototypeState>({
    active: false,
    screen: null,
    start: null,
    canBack: false,
    overlay: null,
    transitioning: false,
  });
  protected readonly prototypeScreens = signal<readonly PrototypeScreen[]>([]);
  protected readonly prototypeChoice = signal('');
  protected readonly prototypeStart = computed(() => {
    const screens = this.prototypeScreens(),
      chosen = this.prototypeChoice(),
      selected = this.selectedNode();
    return (
      screens.find((screen) => screen.id === chosen)?.id ??
      screens.find((screen) => screen.id === selected?.id)?.id ??
      screens.find((screen) => screen.start)?.id ??
      screens[0]?.id ??
      ''
    );
  });
  protected readonly prototypeName = computed(
    () =>
      this.prototypeScreens().find(
        (screen) => screen.id === (this.presentation().overlay ?? this.presentation().screen),
      )?.name || 'Prototype',
  );
  protected readonly prototypeError = signal('');
  protected readonly prototypeWire = signal('');
  protected playPrototype(): void {
    this.prototypeError.set('');
    if (this.inlineText()) {
      this.prototypeError.set('Finish or cancel the text edit before playing.');
      return;
    }
    const core = this.core();
    if (!core || !this.prototypeStart() || !core.startPrototype(this.prototypeStart())) {
      this.prototypeError.set('Create a visible root frame (F), then choose a starting screen.');
      return;
    }
    this.presentation.set(core.prototypeState());
  }
  protected exitPrototype(): void {
    const core = this.core();
    core?.stopPrototype();
    if (core) this.presentation.set(core.prototypeState());
  }
  protected backPrototype(): void {
    const core = this.core();
    core?.prototypeBack();
    if (core) this.presentation.set(core.prototypeState());
  }
  protected restartPrototype(): void {
    const core = this.core();
    core?.restartPrototype();
    if (core) this.presentation.set(core.prototypeState());
  }
  protected closePrototypeOverlay(): void {
    const core = this.core();
    core?.closePrototypeOverlay();
    if (core) this.presentation.set(core.prototypeState());
  }
  protected readonly inlineText = signal<NodeProps | null>(null);
  protected readonly inlineBounds = signal<Rect | null>(null);
  protected beginInlineText(): void {
    if (
      this.presentation().active ||
      this.inlineText() ||
      this.core()?.drawingPath() ||
      this.tool() === 'pen'
    )
      return;
    const core = this.core(),
      selected = core?.selection();
    const node = selected?.length === 1 ? core?.node(selected[0]) : null;
    if (!core || !node || node.type !== 'text' || node.locked || !node.visible) return;
    const bounds = core.screenBounds(node.id);
    if (!bounds) return;
    core.tool = 'select';
    this.inlineBounds.set(bounds);
    this.inlineText.set(node);
  }
  protected readonly saveInlineText = (id: string, text: TextRun): boolean => {
    const current = this.core()?.node(id),
      original = this.inlineText();
    if (
      !current?.text ||
      !original?.text ||
      current.textBound ||
      current.locked ||
      current.text.content !== original.text.content
    )
      return false;
    return this.core()?.setProps(id, { text: { ...current.text, content: text.content } }) ?? false;
  };
  protected readonly componentOptions = computed(() => {
    this.layers();
    const node = this.selectedNode();
    const snapshot = this.core()?.snapshot();
    return node?.type === 'instance' && snapshot
      ? componentOptions(snapshot as ComponentSnapshot, node.id)
      : [];
  });
  protected readonly frameLabels = signal<readonly FrameLabel[]>([]);
  protected readonly sideTab = signal<'layers' | 'tokens' | 'components'>('layers');
  protected readonly componentSnapshot = (): unknown => this.core()?.snapshot();
  protected readonly importImage = (image: RasterImage, name: string): boolean => {
    const id = this.core()?.importImage(image, name);
    if (!id) return false;
    this.tool.set('select');
    this.sideTab.set('layers');
    return true;
  };
  protected readonly saveText = (id: string, text: import('@canvas/bridge').TextRun): boolean =>
    this.core()?.setProps(id, { text }) ?? false;
  protected readonly saveWrap = (id: string, wrap: boolean): boolean =>
    this.core()?.setProps(id, { textWrap: wrap }) ?? false;
  protected readonly saveStyleBinding = (
    id: string,
    patch: import('@canvas/bridge').NodePatch,
  ): boolean => this.core()?.setProps(id, patch) ?? false;
  protected readonly saveComponentDefinition = (
    id: string,
    definition: ComponentDefinition,
  ): boolean => this.core()?.setComponentDefinition(id, definition) ?? false;
  protected readonly saveComponentValues = (id: string, values: ComponentValues): boolean =>
    this.core()?.setProps(id, { componentValues: values }) ?? false;
  protected readonly tokens = signal<readonly TokenRow[]>([]);
  protected readonly modes = signal<readonly ModeRow[]>([]);
  protected readonly activeMode = signal('');
  protected readonly selectedTokenId = signal<string | null>(null);
  protected readonly selectedToken = computed(() =>
    this.sideTab() === 'tokens'
      ? (this.tokens().find((t) => t.id === this.selectedTokenId()) ?? null)
      : null,
  );
  protected readonly stats = signal('Loading visualcore');
  protected readonly syncFailing = signal(false);
  protected readonly status = computed(() =>
    this.syncFailing() ? `Changes not saved, retrying. ${this.stats()}` : this.stats(),
  );

  protected readonly core = signal<VisualCore | undefined>(undefined);
  private destroyed = false;
  private stopFollowingTheme = () => {};
  private labelSizesKey = '';

  constructor() {
    this.destroyRef.onDestroy(() => {
      this.destroyed = true;
      this.stopFollowingTheme();
    });
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
    this.document.update(
      (document) => document && { ...document, name: renamed.name, updatedAt: renamed.updatedAt },
    );
  }

  protected select(id: string): void {
    this.core()?.select(id);
  }

  protected reveal(id: string): void {
    const canvas = document.querySelector<HTMLCanvasElement>('#canvas');
    if (canvas) this.core()?.reveal(id, canvas.clientWidth, canvas.clientHeight);
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
        if (node.component && canvas)
          core.reveal(node.component, canvas.clientWidth, canvas.clientHeight);
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

  protected readonly replaceToken = (token: TokenDef): boolean =>
    this.core()?.replaceToken(token) ?? false;

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

  protected readonly colorToHex = (color: OklchColor): string =>
    this.core()?.colorToHex(color) ?? '#000000';

  protected readonly colorFromHex = (hex: string, alpha: number): OklchColor | null =>
    this.core()?.hexToColor(hex, alpha) ?? null;

  protected onKeydown(event: KeyboardEvent): void {
    const core = this.core();
    if (!core || event.defaultPrevented) return;
    if (this.presentation().active) {
      const key = event.key.toLowerCase(),
        command = event.ctrlKey || event.metaKey;
      if (key === 'escape') {
        this.exitPrototype();
        event.preventDefault();
      } else if (event.altKey && key === 'arrowleft') {
        this.backPrototype();
        event.preventDefault();
      } else if (
        isTyping(event.target) ||
        key === 'tab' ||
        (event.target instanceof HTMLButtonElement && ['enter', ' '].includes(key))
      )
        return;
      else {
        const editing = command
          ? ['z', 'y'].includes(key) || (event.altKey && ['k', 'b'].includes(key))
          : ['delete', 'backspace', 'enter', 'f2'].includes(key) ||
            !!NUDGE_DIRECTIONS[key] ||
            (!event.altKey && !!TOOL_SHORTCUTS[key]);
        if (editing) event.preventDefault();
      }
      return;
    }
    if (isTyping(event.target)) return;

    const key = event.key.toLowerCase();
    const command = event.ctrlKey || event.metaKey;
    let handled = true;
    if (
      !command &&
      core.drawingPath() &&
      ['enter', 'escape', 'backspace', 'delete'].includes(key)
    ) {
      if (key === 'enter') core.finishPath();
      else if (key === 'escape') core.cancelPath();
      else core.popPathPoint();
    } else if (!command && key === 'escape') {
      core.cancelGesture();
    } else if (
      !command &&
      !event.altKey &&
      (key === 'enter' || key === 'f2') &&
      this.selectedNode()?.type === 'text'
    ) {
      this.beginInlineText();
    } else if (command && key === 'z') {
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
      this.tool.set(key === 'p' && event.shiftKey ? 'pencil' : TOOL_SHORTCUTS[key]);
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
      this.stopFollowingTheme = followTheme(core);
      const sync = new DocumentSync(this.api, core, this.id(), (failing) =>
        this.syncFailing.set(failing),
      );
      await sync.open();
      if (this.destroyed) {
        core.detach();
        return;
      }
      this.core.set(core);
      const stopMotionPreference = watchPrototypeMotion(
        core,
        typeof window.matchMedia === 'function'
          ? window.matchMedia('(prefers-reduced-motion: reduce)')
          : undefined,
      );
      let revision = -1;
      let structure = -1;
      let tokenRevision = -1;
      let mode = '';
      let frame = 0;
      const tick = () => {
        const presentation = core.prototypeState();
        if (JSON.stringify(this.presentation()) !== JSON.stringify(presentation))
          this.presentation.set(presentation);
        const editing = this.inlineText();
        if (editing) {
          const bounds = core.screenBounds(editing.id);
          const previous = this.inlineBounds();
          if (!bounds) this.inlineText.set(null);
          else if (JSON.stringify(bounds) !== JSON.stringify(previous))
            this.inlineBounds.set(bounds);
        }
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
          this.prototypeScreens.set(core.prototypeScreens());
          const selection = core.selection();
          this.selectedIds.set(new Set(selection));
          this.selectedNode.set(selection.length === 1 ? core.node(selection[0]) : null);
          this.tool.set(core.tool);
        }
        let wire = '';
        const selectedInteractionNode = this.selectedNode();
        const interaction = selectedInteractionNode?.interactions?.find(
          (entry) => entry.action === 'navigate' || entry.action === 'open-overlay',
        ) ?? selectedInteractionNode?.interaction;
        if (
          !presentation.active &&
          (interaction?.action === 'navigate' || interaction?.action === 'open-overlay')
        ) {
          const selected = this.selectedNode(),
            source = selected ? core.screenBounds(selected.id) : null,
            target = core.screenBounds(interaction.target);
          if (source && target) {
            const x = source.x + source.w,
              y = source.y + source.h / 2,
              tx = target.x,
              ty = target.y + target.h / 2,
              dx = Math.max(40, Math.abs(tx - x) / 2);
            wire = `M ${x} ${y} C ${x + dx} ${y}, ${tx - dx} ${ty}, ${tx} ${ty}`;
          }
        }
        if (this.prototypeWire() !== wire) this.prototypeWire.set(wire);
        frame = requestAnimationFrame(tick);
      };
      frame = requestAnimationFrame(tick);
      const timer = setInterval(
        () => this.stats.set(formatStats(core.frameStats())),
        STATS_INTERVAL_MS,
      );

      this.destroyRef.onDestroy(() => {
        stopMotionPreference();
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
    inInstance: layer.inInstance,
  };
}

function isTyping(target: EventTarget | null): boolean {
  return (
    target instanceof HTMLElement &&
    (target.isContentEditable ||
      target instanceof HTMLInputElement ||
      target instanceof HTMLSelectElement ||
      target instanceof HTMLTextAreaElement)
  );
}

function formatStats({ frameMs, cpuMs, visible, total }: FrameStats): string {
  return `${frameMs.toFixed(1)} ms frame, ${cpuMs.toFixed(2)} ms cpu, ${visible} of ${total} shapes drawn`;
}
