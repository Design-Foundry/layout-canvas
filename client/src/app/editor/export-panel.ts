import { Component, computed, input, signal } from '@angular/core';
import type { ExportedImage } from '@canvas/bridge';
import { LucentButton, LucentSegmentedControl } from '@designfoundry/lucent-angular';
import { EXPORT_SCALES, encodePng, createFileNamer, formatScale } from './export-file';

export interface ExportTarget {
  readonly id: string;
  readonly name: string;
}

export function saveBlob(blob: Blob, fileName: string): void {
  const url = URL.createObjectURL(blob);
  const link = document.createElement('a');
  link.href = url;
  link.download = fileName;
  link.click();
  setTimeout(() => URL.revokeObjectURL(url));
}

const SCALE_OPTIONS = EXPORT_SCALES.map((scale) => ({ label: `${scale}x`, value: String(scale) }));

@Component({
  selector: 'app-export-panel',
  imports: [LucentButton, LucentSegmentedControl],
  styleUrl: './export-panel.scss',
  template: `
    <section class="section">
      <h3 class="heading typography-caption">Export</h3>
      <lucent-segmented-control
        size="sm"
        label="Export scale"
        [options]="scaleOptions"
        [value]="scaleValue()"
        (valueChange)="scale.set(+$any($event))"
      />
      <lucent-button variant="secondary" size="sm" [disabled]="busy()" (clicked)="exportAll()">
        {{ buttonLabel() }}
      </lucent-button>
      @for (line of report(); track $index) {
        <p class="typography-caption" role="status">{{ line }}</p>
      }
      @if (error()) {
        <p class="typography-caption" role="alert">{{ error() }}</p>
      }
    </section>
  `,
})
export class ExportPanel {
  readonly targets = input.required<readonly ExportTarget[]>();
  readonly render = input.required<(id: string, scale: number) => Promise<ExportedImage>>();
  readonly save = input<(blob: Blob, fileName: string) => void>(saveBlob);

  protected readonly scaleOptions = SCALE_OPTIONS;
  protected readonly scale = signal<number>(1);
  protected readonly scaleValue = computed(() => String(this.scale()));
  protected readonly busy = signal(false);
  protected readonly report = signal<readonly string[]>([]);
  protected readonly error = signal('');
  protected readonly buttonLabel = computed(() =>
    this.targets().length > 1 ? `Export ${this.targets().length} layers` : 'Export',
  );

  protected async exportAll(): Promise<void> {
    const targets = this.targets();
    if (!targets.length || this.busy()) return;
    const scale = this.scale();
    const nameFile = createFileNamer();
    this.busy.set(true);
    this.error.set('');
    this.report.set([]);
    const notes: string[] = [];
    try {
      for (const target of targets) {
        const image = await this.render()(target.id, scale);
        this.save()(
          await encodePng(image.width, image.height, image.pixels),
          nameFile(target.name, image.scale),
        );
        if (image.clamped)
          notes.push(`${target.name}: scale reduced to ${formatScale(image.scale)}x`);
        else if (image.tiled) notes.push(`${target.name}: rendered in tiles`);
      }
      this.report.set(notes);
    } catch (reason) {
      this.error.set(reason instanceof Error ? reason.message : 'Export failed');
      this.report.set(notes);
    } finally {
      this.busy.set(false);
    }
  }
}
