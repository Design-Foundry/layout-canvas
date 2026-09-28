import { Component, ElementRef, afterNextRender, signal, viewChild } from '@angular/core';
import { VisualCore } from '@canvas/bridge';

@Component({
  selector: 'app-root',
  styleUrl: './app.scss',
  templateUrl: './app.html',
})
export class App {
  private readonly surface = viewChild.required<ElementRef<HTMLCanvasElement>>('surface');
  protected readonly status = signal('loading visualcore…');

  constructor() {
    afterNextRender(() => void this.start());
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
