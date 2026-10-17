import createModule from '../wasm/visualcore.js';
import type { Camera as CameraHandle, FrameStats, MainModule, Rect, Vec2 } from '../wasm/visualcore.js';

export type { FrameStats, Rect, Vec2 };

export interface LoadOptions {
  /** URL where the host app serves visualcore.wasm. */
  wasmUrl: string;
}

export class VisualCore {
  private constructor(private readonly module: MainModule) {}

  static async load({ wasmUrl }: LoadOptions): Promise<VisualCore> {
    const module = await createModule({
      locateFile: (path: string) => (path.endsWith('.wasm') ? wasmUrl : path),
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

  /** Adds `count` random rects around the origin. Same seed, same rects. */
  addRandomShapes(count: number, seed: number): void {
    this.module.addRandomShapes(count, seed);
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
