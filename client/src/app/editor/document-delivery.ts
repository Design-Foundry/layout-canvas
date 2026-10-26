import { HttpErrorResponse } from '@angular/common/http';
import { Component, inject, input, signal } from '@angular/core';
import { Router } from '@angular/router';
import { Api } from '../api/api';
import { importDocument } from '../api/functions';
import {
  MAX_DOCUMENT_FILE_BYTES,
  documentFilename,
  exportDocument,
  readDocument,
} from './document-package';

@Component({
  selector: 'app-document-delivery',
  template: `<section class="content" aria-label="Local document delivery">
    <h2 class="typography-label-md">Local document</h2>
    <div class="actions">
      <button type="button" [disabled]="busy()" (click)="download()">Export document</button>
      <button type="button" [disabled]="busy()" (click)="file.click()">Open document file</button>
    </div>
    <input #file hidden type="file" accept=".json,application/json" (change)="openFile($event)" />
    <p class="hint typography-caption">
      Complete document, including flows and images. Maximum 2 MB. Reopening creates a separate document.
    </p>
    <details class="hint typography-caption">
      <summary>File and local server details</summary>
      Reopening sends the file to this app's configured Canvas server. Keep the server local for
      local delivery. The file includes authored screens, components and tokens; it needs Canvas
      to reopen and is not a standalone player.
    </details>
    @if (openedUrl()) {
      <a [href]="openedUrl()" target="_blank" rel="noopener">Open imported document in a new tab</a>
    }
    @if (message()) {
      <p role="status" class="typography-caption">{{ message() }}</p>
    }
    @if (error()) {
      <p role="alert" class="error typography-caption">{{ error() }}</p>
    }
  </section>`,
  styleUrl: './components-panel.scss',
})
export class DocumentDelivery {
  readonly snapshot = input.required<() => unknown>();
  readonly name = input.required<string>();
  private readonly api = inject(Api);
  private readonly router = inject(Router);
  protected readonly busy = signal(false);
  protected readonly message = signal('');
  protected readonly error = signal('');
  protected readonly openedUrl = signal('');

  protected download(): void {
    this.error.set('');
    this.message.set('');
    let url: string | undefined;
    try {
      const bundle = exportDocument(this.snapshot()(), this.name());
      url = URL.createObjectURL(new Blob([JSON.stringify(bundle)], { type: 'application/json' }));
      const link = document.createElement('a');
      link.href = url;
      link.download = documentFilename(bundle.name);
      link.click();
      this.message.set(
        'Complete document exported. Playback state and undo history are not included.',
      );
    } catch (error) {
      this.error.set(error instanceof Error ? error.message : 'Could not export this document.');
    } finally {
      if (url) URL.revokeObjectURL(url);
    }
  }

  protected async openFile(event: Event): Promise<void> {
    const input = event.target as HTMLInputElement;
    const file = input.files?.[0];
    input.value = '';
    if (!file || this.busy()) return;
    this.busy.set(true);
    this.error.set('');
    this.message.set('');
    this.openedUrl.set('');
    try {
      if (file.size > MAX_DOCUMENT_FILE_BYTES)
        throw new Error('The Canvas document file exceeds the 2 MB local import limit.');
      const bundle = readDocument(await file.text());
      const created = await this.api.invoke(importDocument, {
        body: { name: bundle.name, snapshot: bundle.snapshot },
      });
      this.openedUrl.set(this.router.serializeUrl(this.router.createUrlTree(['/d', created.id])));
      this.message.set('Document imported. Your current document and undo history are unchanged.');
    } catch (error) {
      const errors = error instanceof HttpErrorResponse ? error.error?.errors : undefined;
      const messages =
        errors && typeof errors === 'object'
          ? Object.values(errors)
              .flat()
              .filter((message) => typeof message === 'string')
          : [];
      this.error.set(
        messages.length
          ? messages.join(' ')
          : error instanceof HttpErrorResponse
            ? 'The Canvas server could not import this file. Your current document is unchanged.'
            : error instanceof Error
              ? error.message
              : 'Could not import this document.',
      );
    } finally {
      this.busy.set(false);
    }
  }
}
