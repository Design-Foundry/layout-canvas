import { Component, OnDestroy, InjectionToken, inject, input, signal } from '@angular/core';
import { importLocalRaster, type LocalRaster } from './local-raster';

export const LOCAL_RASTER_DECODER = new InjectionToken<(file: File) => Promise<LocalRaster>>(
  'Local raster decoder',
  { providedIn: 'root', factory: () => importLocalRaster },
);

@Component({
  selector: 'app-image-import',
  template: `<section aria-label="Local images">
    <h3>Images</h3>
    <button type="button" [disabled]="busy()" (click)="file.click()">
      {{ busy() ? 'Importing image…' : 'Import local image' }}
    </button>
    <input
      #file
      type="file"
      hidden
      accept="image/png,image/jpeg,.png,.jpg,.jpeg"
      (change)="importFile($event)"
    />
    <p>
      Static PNG or JPEG, up to 4 MB and 4096 × 4096. Imported pixels are resized to at most 512 px
      per side and embedded locally. Document limit: 8 distinct images and 1.4 MB of encoded image
      data.
    </p>
    @if (error()) {
      <p role="alert">{{ error() }}</p>
    }
  </section>`,
  styles: `
    section {
      display: grid;
      gap: 8px;
      padding: 12px;
      border-bottom: 1px solid #333;
    }
    h3,
    p {
      margin: 0;
      font: inherit;
    }
    p {
      color: #aaa;
      font-size: 12px;
    }
    button {
      color: inherit;
      background: #292929;
      border: 1px solid #555;
      padding: 6px;
    }
    [role='alert'] {
      color: #ffbd83;
    }
  `,
})
export class ImageImport implements OnDestroy {
  readonly add = input.required<(image: LocalRaster, name: string) => boolean>();
  protected readonly busy = signal(false);
  protected readonly error = signal('');
  private readonly decode = inject(LOCAL_RASTER_DECODER);
  private destroyed = false;
  ngOnDestroy(): void {
    this.destroyed = true;
  }
  protected async importFile(event: Event): Promise<void> {
    const input = event.target as HTMLInputElement,
      file = input.files?.[0];
    if (!file || this.busy()) return;
    this.busy.set(true);
    this.error.set('');
    try {
      const image = await this.decode(file);
      if (!this.destroyed && !this.add()(image, file.name.slice(0, 128)))
        this.error.set(
          'The image was refused. Check the document image budget; the saved document was kept.',
        );
    } catch (error) {
      if (!this.destroyed)
        this.error.set(error instanceof Error ? error.message : 'Image import failed.');
    } finally {
      input.value = '';
      if (!this.destroyed) this.busy.set(false);
    }
  }
}
