import type createModule from '../wasm/visualcore.js';
import type {
  Camera as CameraHandle,
  FrameStats,
  MainModule,
  Rect,
  Vec2,
} from '../wasm/visualcore.js';

export type { FrameStats, Rect, Vec2 };

export type Tool =
  | 'select'
  | 'frame'
  | 'rectangle'
  | 'ellipse'
  | 'hand'
  | 'text'
  | 'line'
  | 'pen'
  | 'pencil'
  | 'polygon'
  | 'star';

export type NodeType =
  | 'frame'
  | 'rect'
  | 'ellipse'
  | 'component'
  | 'instance'
  | 'text'
  | 'line'
  | 'path'
  | 'image';

export interface TextRun {
  readonly content: string;
  readonly font:
    | 'inter-regular-v1'
    | 'inter-bold-v1'
    | 'inter-oblique-v1'
    | 'inter-bold-oblique-v1';
  readonly fontSize: number;
  readonly lineHeight: number;
  readonly letterSpacing: number;
  readonly align: 'left' | 'center' | 'right';
}

/** One row of the layers panel. Rows come topmost first, each followed by its children. */
export interface Layer {
  readonly id: string;
  readonly name: string;
  readonly type: NodeType;
  /** Nesting level, 0 for top-level nodes. */
  readonly depth: number;
  readonly visible: boolean;
  readonly locked: boolean;
  /** True for a node copied into an instance. */
  readonly inInstance?: boolean;
}

/** A top-level frame's name on the canvas: its top-left corner and width in screen CSS pixels. */
export interface FrameLabel {
  readonly id: string;
  readonly name: string;
  readonly type: NodeType;
  readonly x: number;
  readonly y: number;
  readonly w: number;
}

/** A color in OKLCH: lightness 0 to 1, chroma, hue in degrees, alpha 0 to 1. */
export interface OklchColor {
  readonly l: number;
  readonly c: number;
  readonly h: number;
  readonly a: number;
}

/** A reference to a design token by id. */
export interface TokenValue {
  readonly token: string;
}

/** A number of canvas pixels, or a dimension (or opacity) token. */
export type Length = number | TokenValue;

export type ColorValue = OklchColor | TokenValue;

export type StrokeStyle = 'none' | 'solid' | 'dashed' | 'dotted' | 'double';
export type StrokeAlign = 'inside' | 'center' | 'outside';
export type LayoutType = 'none' | 'row' | 'column';
export type Justify = 'start' | 'center' | 'end' | 'spaceBetween' | 'spaceAround' | 'spaceEvenly';
export type AlignItems = 'start' | 'center' | 'end' | 'stretch';
export type Sizing = 'fixed' | 'hug' | 'fill';

/** Sides and corners go top, end, bottom, start; corners top-start, top-end, bottom-end, bottom-start. */
export type Four<T> = readonly [T, T, T, T];

export interface StrokeProps {
  readonly width: Length;
  /** Per-side overrides of `width`; null keeps `width`. */
  readonly sides?: Four<Length | null>;
  readonly style: StrokeStyle;
  readonly color: ColorValue;
  readonly align: StrokeAlign;
}

export interface ShadowProps {
  readonly x: number;
  readonly y: number;
  readonly blur: number;
  readonly spread: number;
  readonly color: ColorValue;
  readonly inset: boolean;
  readonly visible: boolean;
}

export interface LayoutProps {
  readonly type: LayoutType;
  readonly gap: Length;
  readonly padding: Four<Length>;
  readonly justify: Justify;
  readonly align: AlignItems;
}

export interface SizeLimits {
  readonly minWidth?: Length;
  readonly maxWidth?: Length;
  readonly minHeight?: Length;
  readonly maxHeight?: Length;
}

export type ComponentValues = Readonly<Record<string, boolean | string>>;
export interface BooleanProperty {
  readonly name: string;
  readonly target: string;
  readonly default: boolean;
}
export interface VariantAxis {
  readonly name: string;
  readonly values: readonly string[];
  readonly default: string;
}
export interface VariantRule {
  readonly when: Readonly<Record<string, string>>;
  readonly target: string;
  readonly patch: NodePatch;
}
export interface ComponentDefinition {
  readonly texts?: readonly TextProperty[];
  readonly swaps?: readonly InstanceSwapProperty[];
  readonly properties: readonly BooleanProperty[];
  readonly axes: readonly VariantAxis[];
  readonly rules: readonly VariantRule[];
}

export interface TextProperty {
  readonly name: string;
  readonly target: string;
  readonly default: string;
}

export interface InstanceSwapProperty {
  readonly name: string;
  readonly target: string;
  readonly default: string;
  readonly choices: readonly string[];
}

export interface RasterImage {
  readonly id: string;
  readonly width: number;
  readonly height: number;
  readonly pixels: string;
}
export type ImageFit = 'cover' | 'contain' | 'stretch';
export interface ImageCrop {
  readonly x: number;
  readonly y: number;
  readonly w: number;
  readonly h: number;
}

/** The properties a node stores. */
export interface NodeFields {
  readonly image?: RasterImage;
  readonly imageFit?: ImageFit;
  readonly imageCrop?: ImageCrop;
  readonly path?: {
    readonly points: readonly { readonly x: number; readonly y: number }[];
    readonly closed: boolean;
    readonly inHandles?: readonly { readonly x: number; readonly y: number }[];
    readonly outHandles?: readonly { readonly x: number; readonly y: number }[];
    readonly filled?: boolean;
  };
  readonly lineWidth?: number;
  readonly lineReverse?: boolean;
  readonly typography?: string;
  readonly textWrap?: boolean;
  readonly text?: TextRun;
  readonly componentValues?: ComponentValues;
  readonly name: string;
  readonly x: number;
  readonly y: number;
  readonly w: number;
  readonly h: number;
  readonly fill: ColorValue;
  readonly fillVisible: boolean;
  readonly cornerRadius: Length;
  readonly cornerRadii: Four<Length | null>;
  readonly stroke: StrokeProps | TokenValue;
  readonly shadows: readonly ShadowProps[] | TokenValue;
  readonly opacity: Length;
  readonly visible: boolean;
  readonly locked: boolean;
  readonly clipContent: boolean;
  readonly layout: LayoutProps;
  readonly widthSizing: Sizing;
  readonly heightSizing: Sizing;
  readonly limits: SizeLimits;
  readonly absolute: boolean;
}

/** A node as the inspector sees it: every field, plus its laid-out box and how its parent places it. */
export interface NodeProps extends Omit<NodeFields, 'fill'> {
  readonly textBound?: boolean;
  readonly swapBound?: boolean;
  readonly definition?: ComponentDefinition;
  readonly id: string;
  readonly type: NodeType;
  readonly parent?: string;
  /** A literal fill also carries its sRGB hex. */
  readonly fill: (OklchColor & { readonly hex: string }) | TokenValue;
  /** The box after auto layout, relative to the parent. */
  readonly box: { readonly x: number; readonly y: number; readonly w: number; readonly h: number };
  /** True when an auto-layout parent positions it. */
  readonly inFlow: boolean;
  readonly parentLayout: LayoutType;
  /** On an instance: the component it copies. */
  readonly component?: string;
  readonly componentName?: string;
  /** On an instance: overridden-node count. On a copied layer: whether it has its own local override (0/1). */
  readonly overrides?: number;
  /** On a component: true when instances of it exist. */
  readonly inUse?: boolean;
  /** True for a node copied into an instance. Its edits become overrides. */
  readonly inInstance?: boolean;
}

/** The properties a `setProps` call may change. Grouped properties are replaced whole. */
export type NodePatch = Partial<NodeFields> & {
  readonly component?: string;
  readonly textStyle?: Partial<Omit<TextRun, 'content'>>;
};

export type TokenType =
  | 'color'
  | 'dimension'
  | 'duration'
  | 'ratio'
  | 'opacity'
  | 'border'
  | 'shadow'
  | 'typography';

export type TypographyValue = Omit<TextRun, 'content'>;

export type TokenUnit = 'px' | 'rem' | 'em' | 'pt' | 'ms' | 's';

/** A token's own color. Colors are born solid; alpha comes from modifiers. */
export interface TokenColor {
  readonly l: number;
  readonly c: number;
  readonly h: number;
}

/** A number with its unit. Ratio and opacity tokens have no unit. */
export interface TokenMagnitude {
  readonly value: number;
  readonly unit?: TokenUnit;
}

export type ColorModifier =
  | {
      readonly channel: 'l' | 'c' | 'h' | 'alpha';
      readonly kind: 'set' | 'delta' | 'factor';
      readonly value?: number;
      readonly ref?: string;
    }
  | { readonly mix: string; readonly amount: number };

/** A border token: ids of a dimension token and a color token, plus a line style. */
export interface TokenBorder {
  readonly width: string;
  readonly style: StrokeStyle;
  readonly color: string;
}

export interface TokenShadowLayer {
  readonly offsetX: number;
  readonly offsetY: number;
  readonly blur: number;
  readonly spread: number;
  readonly inset: boolean;
  /** Id of a color token. */
  readonly color: string;
  readonly colorModifiers?: readonly ColorModifier[];
}

export interface TokenShadow {
  readonly layers: readonly TokenShadowLayer[];
}

export interface MagnitudeModifier {
  readonly kind: 'factor' | 'delta' | 'min' | 'max';
  readonly value?: number;
  readonly ref?: string;
}

/**
 * A design token as Design Foundry defines it: a primitive `value`, or an alias `source` mapping each
 * mode id to a token id of the same family (the default mode is required). Modifiers apply after.
 * Local typography composites currently accept complete literal values only.
 */
export interface TokenDef {
  readonly id: string;
  readonly type: TokenType;
  readonly identifier: string;
  readonly order: string;
  readonly displayName?: string;
  readonly description?: string;
  readonly deprecated?: boolean;
  readonly value?: TokenColor | TokenMagnitude | TokenBorder | TokenShadow | TypographyValue;
  readonly source?: Readonly<Record<string, string>>;
  readonly modifiers?: readonly (ColorModifier | MagnitudeModifier)[];
}

export interface ResolvedColor extends TokenColor {
  readonly a: number;
  readonly hex: string;
}

/** A border token resolved in the active mode: width in pixels and the final color. */
export interface ResolvedBorder {
  readonly width: number;
  readonly style: StrokeStyle;
  readonly color: ResolvedColor;
}

/** A shadow token resolved in the active mode, colors after their modifiers. */
export interface ResolvedShadow {
  readonly layers: readonly {
    readonly x: number;
    readonly y: number;
    readonly blur: number;
    readonly spread: number;
    readonly inset: boolean;
    readonly color: ResolvedColor;
  }[];
}

/** A token as the tokens panel shows it: its definition, its value in the active mode, and whether it is in use. */
export interface TokenRow extends TokenDef {
  readonly resolved?:
    | ResolvedColor
    | TokenMagnitude
    | ResolvedBorder
    | ResolvedShadow
    | TypographyValue;
  readonly referenced: boolean;
}

export interface ModeRow {
  readonly id: string;
  readonly name: string;
  readonly order: string;
  readonly isDefault: boolean;
}

/** A local edit waiting to be sent to the server. */
export interface OutgoingOp {
  readonly id: string;
  readonly kind: string;
  readonly data: unknown;
}

/** An op read from the server log. Extra fields are ignored. */
export interface IncomingOp {
  /** The id the op was posted with, so visualcore recognizes its own edits coming back. */
  readonly id: string;
  readonly kind: string;
  readonly data: unknown;
}

export interface LoadOptions {
  /**
   * URL of the folder where the host app serves `visualcore.js` and `visualcore.wasm` side by side.
   * Both are loaded from there at runtime, so a rebuilt module is picked up on reload.
   */
  baseUrl: string;
}

export class VisualCore {
  private lastLabels = '';

  private constructor(private readonly module: MainModule) {}

  static async load({ baseUrl }: LoadOptions): Promise<VisualCore> {
    const base = new URL(baseUrl.endsWith('/') ? baseUrl : `${baseUrl}/`, document.baseURI);
    const glue: { default: typeof createModule } = await import(
      /* @vite-ignore */ new URL('visualcore.js', base).href
    );
    const module = await glue.default({
      locateFile: (path: string) => new URL(path, base).href,
    });
    return new VisualCore(module);
  }

  /** Takes over the canvas matching `selector`: rendering and pointer input run in WASM from here on. */
  attach(selector: string): void {
    this.module.attach(selector);
  }

  /** Stops the frame loop and removes the input listeners. */
  detach(): void {
    this.module.detach();
  }

  /** Adds `count` random nodes around the origin, outside the undo history and the outbox. */
  addRandomShapes(count: number, seed: number): void {
    this.module.addRandomShapes(count, seed);
  }

  /** Replaces the document with `snapshot` (or an empty one), dropping history and pending ops. */
  loadDocument(snapshot: unknown | null): boolean {
    return this.module.loadDocument(snapshot == null ? '' : JSON.stringify(snapshot));
  }

  /**
   * Applies a page of the server log, in log order, beneath the local edits it doesn't confirm yet.
   * Returns how many applied, or -1 if the batch was malformed.
   */
  applyRemoteOps(ops: readonly IncomingOp[]): number {
    return this.module.applyRemoteOps(JSON.stringify(ops));
  }

  /** Hands over the local edits made since the last call, in order. */
  takeOutbox(): OutgoingOp[] {
    return JSON.parse(this.module.takeOutbox());
  }

  /** The whole document, to store as a server snapshot. */
  snapshot(): unknown {
    return JSON.parse(this.module.snapshot());
  }

  /** Laid-out node bounds in the attached canvas's CSS pixels, including camera pan/zoom. */
  screenBounds(id: string): Rect | null {
    return JSON.parse(this.module.screenBounds(id));
  }

  /** Changes whenever the document, the selection or the tool changes. Cheap to poll every frame. */
  revision(): number {
    return this.module.revision();
  }

  /** Changes only when the layer tree changes: nodes added, removed or moved, renamed or hidden. */
  structureRevision(): number {
    return this.module.structureRevision();
  }

  /** Frame labels, or null when nothing moved since the last call. Cheap to poll every frame. */
  frameLabels(): FrameLabel[] | null {
    const json = this.module.frameLabels();
    if (json === this.lastLabels) return null;
    this.lastLabels = json;
    return JSON.parse(json);
  }

  layers(): Layer[] {
    return JSON.parse(this.module.layers());
  }

  selection(): string[] {
    return JSON.parse(this.module.selection());
  }

  select(id: string): void {
    this.module.select(id);
  }

  /** Adds `id` to the selection, or removes it if it was there. */
  toggleSelection(id: string): void {
    this.module.toggleSelection(id);
  }

  /** Measured sizes of the frame labels the page draws, so pressing a label selects its frame. */
  setFrameLabelSizes(sizes: readonly { id: string; w: number; h: number }[]): void {
    this.module.setFrameLabelSizes(sizes.map((s) => `${s.id}:${s.w}:${s.h}`).join(';'));
  }

  get tool(): Tool {
    return this.module.tool() as Tool;
  }

  set tool(tool: Tool) {
    this.module.setTool(tool);
  }

  importImage(image: RasterImage, name: string): string {
    return this.module.importImage(JSON.stringify(image), name);
  }

  /** Ends a Pen gesture as one edit. */
  finishPath(): boolean {
    return this.module.finishPath();
  }
  /** Cancels the active pointer gesture, preserving the saved document. */
  cancelGesture(): void {
    this.module.cancelGesture();
  }

  cancelPath(): void {
    this.module.cancelPath();
  }
  popPathPoint(): boolean {
    return this.module.popPathPoint();
  }
  drawingPath(): boolean {
    return this.module.drawingPath();
  }

  node(id: string): NodeProps | null {
    return JSON.parse(this.module.node(id));
  }

  /** Changes `id` as one undo step. Returns false if the patch was rejected. */
  setProps(id: string, patch: NodePatch): boolean {
    return this.module.setProps(id, JSON.stringify(patch));
  }

  setComponentDefinition(id: string, definition: ComponentDefinition): boolean {
    return this.module.setComponentDefinition(id, JSON.stringify(definition));
  }

  /**
   * Shows `patch` on `id` live without recording it, replacing the previous preview. Finish with
   * `commitPreview` (one undo step) or `cancelPreview`. Any other edit commits it first.
   */
  previewProps(id: string, patch: NodePatch): boolean {
    return this.module.previewProps(id, JSON.stringify(patch));
  }

  commitPreview(): boolean {
    return this.module.commitPropsPreview();
  }

  cancelPreview(): void {
    this.module.cancelPropsPreview();
  }

  /** Changes only when tokens or modes change. */
  tokenRevision(): number {
    return this.module.tokenRevision();
  }

  /** Tokens grouped by family, each resolved in the active mode. */
  tokens(): TokenRow[] {
    return JSON.parse(this.module.tokens());
  }

  modes(): ModeRow[] {
    return JSON.parse(this.module.modes());
  }

  /** The mode the canvas and the tokens panel resolve in. Switching it is not an edit. */
  get activeMode(): string {
    return this.module.activeMode();
  }

  set activeMode(id: string) {
    this.module.setActiveMode(id);
  }

  /** Adds a token with a starting value and a free identifier. Returns its id. */
  addToken(type: TokenType): string | null {
    return this.module.addToken(type) || null;
  }

  /** Replaces a token's definition as one undo step. Returns false if it was invalid. */
  replaceToken(token: TokenDef): boolean {
    return this.module.replaceToken(JSON.stringify(token));
  }

  /** Deletes a token nothing references. */
  deleteToken(id: string): boolean {
    return this.module.deleteToken(id);
  }

  addMode(): string | null {
    return this.module.addMode() || null;
  }

  replaceMode(mode: Omit<ModeRow, 'isDefault'>): boolean {
    return this.module.replaceMode(JSON.stringify(mode));
  }

  /** Deletes a mode, removing it from every alias first. The default mode can't be deleted. */
  deleteMode(id: string): boolean {
    return this.module.deleteMode(id);
  }

  /** The sRGB hex of an OKLCH color, ignoring alpha. */
  colorToHex(color: OklchColor): string {
    return this.module.colorToHex(JSON.stringify(color));
  }

  /** Converts `#rgb` or `#rrggbb` to OKLCH, or returns null if it isn't a hex color. */
  hexToColor(hex: string, alpha = 1): OklchColor | null {
    return JSON.parse(this.module.hexToColor(hex, alpha));
  }

  /** Moves the selection by `dx`, `dy` world units as one undo step. */
  nudge(dx: number, dy: number): boolean {
    return this.module.nudge(dx, dy);
  }

  /** Turns the selected frame into a component, or wraps the selection in one. Returns its id. */
  createComponent(): string | null {
    return this.module.createComponent() || null;
  }

  /** Places a new instance of `component` next to it and selects it. */
  createInstance(component: string): string | null {
    return this.module.createInstance(component) || null;
  }

  /** Replaces an instance with ordinary nodes that look the same. Returns the new frame. */
  detachInstance(instance: string): string | null {
    return this.module.detachInstance(instance) || null;
  }

  resetOverrides(instance: string): boolean {
    return this.module.resetOverrides(instance);
  }

  /** Selects `id` and pans to center it when it isn't fully inside a viewport of that size. */
  reveal(id: string, width: number, height: number): boolean {
    return this.module.reveal(id, width, height);
  }

  deleteSelection(): boolean {
    return this.module.deleteSelection();
  }

  undo(): boolean {
    return this.module.undo();
  }

  redo(): boolean {
    return this.module.redo();
  }

  /** Smoothed timings of the last frames and how many shapes were drawn. */
  frameStats(): FrameStats {
    return this.module.frameStats();
  }

  /** The caller owns the camera: `using cam = core.createCamera()` or call `dispose()`. */
  createCamera(): Camera {
    return new Camera(new this.module.Camera());
  }
}

export class Camera implements Disposable {
  constructor(private readonly handle: CameraHandle) {}

  get zoom(): number {
    return this.handle.zoom();
  }
  get position(): Vec2 {
    return this.handle.position();
  }

  toScreen(world: Vec2): Vec2 {
    return this.handle.toScreen(world);
  }
  toWorld(screen: Vec2): Vec2 {
    return this.handle.toWorld(screen);
  }
  visibleWorld(screenSize: Vec2): Rect {
    return this.handle.visibleWorld(screenSize);
  }
  pan(screenDelta: Vec2): void {
    this.handle.pan(screenDelta);
  }
  zoomAt(screenPoint: Vec2, factor: number): void {
    this.handle.zoomAt(screenPoint, factor);
  }

  dispose(): void {
    if (!this.handle.isDeleted()) this.handle.delete();
  }
  [Symbol.dispose](): void {
    this.dispose();
  }
}
