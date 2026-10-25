import { Injectable, signal } from '@angular/core';

export type ThemePreference = 'system' | 'light' | 'dark';

const storageKey = 'canvas.theme';
const order: readonly ThemePreference[] = ['system', 'light', 'dark'];

@Injectable({ providedIn: 'root' })
export class Theme {
  readonly preference = signal<ThemePreference>(this.stored());

  constructor() {
    this.apply(this.preference());
  }

  cycle(): void {
    const next = order[(order.indexOf(this.preference()) + 1) % order.length];
    this.preference.set(next);
    this.apply(next);
    try {
      localStorage.setItem(storageKey, next);
    } catch {}
  }

  private stored(): ThemePreference {
    try {
      const value = localStorage.getItem(storageKey);
      return order.find((p) => p === value) ?? 'system';
    } catch {
      return 'system';
    }
  }

  private apply(preference: ThemePreference): void {
    const root = document.documentElement;
    if (preference === 'system') root.removeAttribute('data-theme');
    else root.setAttribute('data-theme', preference);
  }
}
