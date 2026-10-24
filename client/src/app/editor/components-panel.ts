import { HttpErrorResponse } from '@angular/common/http';
import { Component, computed, inject, input, output, signal } from '@angular/core';
import { FormControl, ReactiveFormsModule } from '@angular/forms';
import { Router } from '@angular/router';
import { LucentButton, LucentLabel, LucentTextInput } from '@design-foundry/lucent';
import { LayerRow } from './editor-model';
import {
  ComponentPackage,
  MAX_COMPONENT_BYTES,
  exportComponent,
  readComponent,
} from './component-package';
import { Api } from '../api/api';
import { importDocument } from '../api/functions';

@Component({
  selector: 'app-components-panel',
  imports: [ReactiveFormsModule, LucentButton, LucentLabel, LucentTextInput],
  template: `<section class="content">
    <h2 class="typography-label-md">Components</h2>
    <lucent-text-input size="sm" placeholder="Search components" [formControl]="search"
      ><span lucentLabel>Search components</span></lucent-text-input
    >
    <lucent-button variant="secondary" size="sm" [disabled]="busy()" (clicked)="file.click()"
      >Import Canvas component</lucent-button
    >
    <input #file hidden type="file" accept=".json,application/json" (change)="importFile($event)" />
    <p class="hint typography-caption">
      Imported components open in a new document, with their tokens and main components.
    </p>
    @for (component of filtered(); track component.id) {
      <article>
        <button
          class="component-name typography-label-sm"
          type="button"
          (click)="reveal.emit(component.id)"
        >
          {{ component.name }}
        </button>
        <div class="actions">
          <lucent-button
            variant="secondary"
            size="sm"
            [disabled]="busy()"
            (clicked)="instantiate.emit(component.id)"
            >Use</lucent-button
          >
          <lucent-button
            variant="ghost"
            size="sm"
            [disabled]="busy()"
            (clicked)="download(component.id)"
            >Export</lucent-button
          >
        </div>
      </article>
    } @empty {
      <p class="hint typography-body-sm">
        Create a component from a selection with Ctrl+Alt+K, or import a Canvas component file.
      </p>
    }
    @if (importedUrl()) {
      <a class="typography-label-sm" [href]="importedUrl()" target="_blank" rel="noopener"
        >Open imported component in a new tab</a
      >
    }
    @if (message()) {
      <p class="typography-caption" role="status">{{ message() }}</p>
    }
    @if (error()) {
      <p class="error typography-caption" role="alert">{{ error() }}</p>
    }
  </section>`,
  styleUrl: './components-panel.scss',
})
export class ComponentsPanel {
  readonly layers = input.required<readonly LayerRow[]>();
  readonly snapshot = input.required<() => unknown>();
  readonly instantiate = output<string>();
  readonly reveal = output<string>();
  private readonly api = inject(Api);
  private readonly router = inject(Router);
  protected readonly search = new FormControl('', { nonNullable: true });
  protected readonly busy = signal(false);
  protected readonly message = signal('');
  protected readonly error = signal('');
  protected readonly importedUrl = signal('');
  protected readonly components = computed(() =>
    this.layers().filter((layer) => layer.kind === 'component' && !layer.inInstance),
  );
  protected filtered(): readonly LayerRow[] {
    const query = this.search.value.trim().toLowerCase();
    return this.components().filter((layer) => layer.name.toLowerCase().includes(query));
  }
  protected download(id: string): void {
    this.run(() => this.save(exportComponent(this.snapshot()(), id), 'canvas-component.json'));
  }
  protected async importFile(event: Event): Promise<void> {
    const input = event.target as HTMLInputElement;
    const file = input.files?.[0];
    input.value = '';
    if (!file || this.busy()) return;
    this.error.set('');
    this.message.set('');
    this.busy.set(true);
    this.importedUrl.set('');
    try {
      if (file.size > MAX_COMPONENT_BYTES) throw new Error('The component file exceeds 2 MB.');
      const bundle = readComponent(await file.text());
      await this.createImportedDocument(bundle);
    } catch (error) {
      this.error.set(this.importError(error));
    } finally {
      this.busy.set(false);
    }
  }

  private async createImportedDocument(bundle: ComponentPackage): Promise<void> {
    const created = await this.api.invoke(importDocument, {
      body: { name: bundle.name, snapshot: bundle.snapshot },
    });
    // Keep the current editor alive, including its unsent edits and undo history.
    this.importedUrl.set(this.router.serializeUrl(this.router.createUrlTree(['/d', created.id])));
    this.message.set('Component imported. Open the new document to edit it.');
  }

  private importError(error: unknown): string {
    if (error instanceof HttpErrorResponse) {
      const errors = error.error?.errors;
      if (errors && typeof errors === 'object') {
        const messages = Object.values(errors)
          .flat()
          .filter((message) => typeof message === 'string');
        if (messages.length) return messages.join(' ');
      }
      return 'The Canvas server could not import this component. Nothing in the current document was changed.';
    }
    return error instanceof Error ? error.message : 'Could not import this component.';
  }

  private run(action: () => void): void {
    this.error.set('');
    this.message.set('');
    try {
      action();
    } catch (error) {
      this.error.set(error instanceof Error ? error.message : 'Could not export the component.');
    }
  }

  private save(data: unknown, filename: string): void {
    const url = URL.createObjectURL(
      new Blob([JSON.stringify(data)], { type: 'application/json' }),
    );
    const link = document.createElement('a');
    link.href = url;
    link.download = filename;
    link.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  }
}
