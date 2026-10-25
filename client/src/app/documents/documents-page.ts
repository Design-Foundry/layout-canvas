import { Component, computed, inject, resource, signal } from '@angular/core';
import { DatePipe } from '@angular/common';
import { Router } from '@angular/router';
import {
  LucentButton,
  LucentIcon,
  LucentIconButton,
  LucentList,
  LucentListItem,
  LucentPrefix,
  LucentSpinner,
  LucentSupporting,
  LucentTrailing,
} from '@designfoundry/lucent-angular';
import { Api } from '../api/api';
import { createDocument, deleteDocument, listDocuments } from '../api/functions';
import { DocumentSummary } from '../api/models';
import { Theme } from '../theme';

@Component({
  selector: 'app-documents-page',
  imports: [
    DatePipe,
    LucentButton,
    LucentIcon,
    LucentIconButton,
    LucentList,
    LucentListItem,
    LucentPrefix,
    LucentSpinner,
    LucentSupporting,
    LucentTrailing,
  ],
  templateUrl: './documents-page.html',
  styleUrl: './documents-page.scss',
})
export class DocumentsPage {
  private readonly api = inject(Api);
  private readonly router = inject(Router);

  protected readonly documents = resource({ loader: () => this.api.invoke(listDocuments) });
  protected readonly creating = signal(false);
  protected readonly theme = inject(Theme);
  protected readonly themeIcon = computed(
    () => ({ system: 'contrast', light: 'light_mode', dark: 'dark_mode' })[this.theme.preference()],
  );

  protected async create(): Promise<void> {
    this.creating.set(true);
    try {
      const document = await this.api.invoke(createDocument, { body: { name: 'Untitled' } });
      await this.router.navigate(['/d', document.id]);
    } finally {
      this.creating.set(false);
    }
  }

  protected open(id: string): void {
    void this.router.navigate(['/d', id]);
  }

  protected async remove(document: DocumentSummary, event: MouseEvent): Promise<void> {
    event.stopPropagation();
    if (!confirm(`Delete "${document.name}"?`)) {
      return;
    }
    await this.api.invoke(deleteDocument, { id: document.id });
    this.documents.reload();
  }
}
