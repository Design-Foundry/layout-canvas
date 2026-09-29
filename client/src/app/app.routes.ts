import { Routes } from '@angular/router';

export const routes: Routes = [
  {
    path: '',
    title: 'Canvas',
    loadComponent: () => import('./documents/documents-page').then((m) => m.DocumentsPage),
  },
  {
    path: 'd/:id',
    loadComponent: () => import('./editor/editor-page').then((m) => m.EditorPage),
  },
  { path: '**', redirectTo: '' },
];
