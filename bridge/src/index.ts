import type createModule from '../wasm/visualcore.js';
import type { Camera as CameraHandle, FrameStats, MainModule, Rect, Vec2 } from '../wasm/visualcore.js';

export type { FrameStats, Rect, Vec2 };

export type Tool = 'select' | 'frame' | 'rectangle' | 'ellipse' | 'hand';

export type NodeType = 'frame' | 'rect' | 'ellipse' | 'instance';

/** One row of the layers panel. Rows come topmost first, each followed by its children. */
export interface Layer {
  readonly id: string;
  readonly name: string;
  readonly type: NodeType;
  /** Nesting level, 0 for top-level nodes. */
  readonly depth: number;
  readonly visible: boolean;
}

/** A color in OKLCH: lightness 0 to 1, chroma, hue in degrees, alpha 0 to 1. */
export interface OklchColor {
  readonly l: number;
  readonly c: number;
  readonly h: number;
  readonly a: number;
}

/** A reference to a design token by name. */
export interface TokenValue {
  readonly token: string;
}

/** A node as the inspector sees it. A literal fill also carries its sRGB hex. */
export interface NodeProps {
  readonly id: string;
  readonly type: NodeType;
  readonly parent?: string;
  readonly name: string;
  /** Position relative to the parent frame. */
  readonly x: number;
  readonly y: number;
  readonly w: number;
  readonly h: number;
  readonly fill: (OklchColor & { readonly hex: string }) | TokenValue;
  readonly cornerRadius: number | TokenValue;
  readonly opacity: number;
  readonly visible: boolean;
  readonly clipContent: boolean;
}

/** The properties a `setProps` call may change. Absent fields stay as they are. */
export interface NodePatch {
  readonly name?: string;
  readonly x?: number;
  readonly y?: number;
  readonly w?: number;
  readonly h?: number;
  readonly fill?: OklchColor | TokenValue;
  readonly cornerRadius?: number | TokenValue;
  readonly opacity?: number;
  readonly visible?: boolean;
  readonly clipContent?: boolean;
}

/** A local edit waiting to be sent to the server. */
export interface OutgoingOp {
  readonly id: string;
  readonly kind: string;
  readonly data: unknown;
}

/** An op read from the server log. Extra fields are ignored. */
export interface IncomingOp {
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
  private constructor(private readonly module: MainModule) {}

  static async load({ baseUrl }: LoadOptions): Promise<VisualCore> {
    const base = new URL(baseUrl.endsWith('/') ? baseUrl : `${baseUrl}/`, document.baseURI);
    const glue: { default: typeof createModule } = await import(/* @vite-ignore */ new URL('visualcore.js', base).href);
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

  /** Applies ops from the server log. Returns how many applied, or -1 if the batch was malformed. */
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

  /** Changes whenever the document, the selection or the tool changes. Cheap to poll every frame. */
  revision(): number {
    return this.module.revision();
  }

  /** Changes only when the layer tree changes: nodes added, removed or moved, renamed or hidden. */
  structureRevision(): number {
    return this.module.structureRevision();
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

  get tool(): Tool {
    return this.module.tool() as Tool;
  }

  set tool(tool: Tool) {
    this.module.setTool(tool);
  }

  node(id: string): NodeProps | null {
    return JSON.parse(this.module.node(id));
  }

  /** Changes `id` as one undo step. Returns false if the patch was rejected. */
  setProps(id: string, patch: NodePatch): boolean {
    return this.module.setProps(id, JSON.stringify(patch));
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

  /** Converts `#rgb` or `#rrggbb` to OKLCH, or returns null if it isn't a hex color. */
  hexToColor(hex: string, alpha = 1): OklchColor | null {
    return JSON.parse(this.module.hexToColor(hex, alpha));
  }

  /** Moves the selection by `dx`, `dy` world units as one undo step. */
  nudge(dx: number, dy: number): boolean {
    return this.module.nudge(dx, dy);
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
