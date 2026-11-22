import { HttpClient, HttpErrorResponse } from '@angular/common/http';
import { Injectable, Injector, inject, signal } from '@angular/core';
import { firstValueFrom } from 'rxjs';
import type { IconRef, VisualCore } from '@canvas/bridge';

type IconCore = Pick<VisualCore, 'usedIcons' | 'registerIcon'>;

export interface GlyphSearch {
  readonly glyphs: readonly string[];
  readonly total: number;
}

export const ICON_STYLES = ['outlined', 'rounded', 'sharp'] as const;
export const ICON_WEIGHTS = [100, 200, 300, 400, 500, 600, 700] as const;
const RETRY_AFTER_MS = 60_000;

/** The query that asks the server for the SVG of `icon`: only the axes the icon sets. */
export function glyphParams(icon: IconRef): Record<string, string | number> {
  const params: Record<string, string | number> = {};
  for (const [tag, value] of Object.entries(icon.axes ?? {})) params[tag] = value;
  return params;
}

/**
 * Loads the SVG of every icon the document draws from the server and registers it with the renderer,
 * and searches glyph names. An icon the source does not have is left to draw as a missing glyph.
 */
@Injectable({ providedIn: 'root' })
export class IconService {
  private readonly injector = inject(Injector);
  private readonly settled = new Map<string, number | null>();
  private core: IconCore | undefined;

  /** Counts the icons whose SVG finished loading. */
  readonly loadedIcons = signal(0);

  private get http(): HttpClient {
    return this.injector.get(HttpClient);
  }

  attach(core: IconCore | undefined): void {
    if (core !== this.core) this.settled.clear();
    this.core = core;
    if (core) this.refresh();
  }

  /** Starts loading the icons the document draws that are not loaded yet. */
  refresh(): void {
    const core = this.core;
    if (!core) return;
    for (const { key, icon } of core.usedIcons()) {
      const failedAt = this.settled.get(key);
      if (failedAt === null || (failedAt !== undefined && Date.now() - failedAt < RETRY_AFTER_MS)) continue;
      this.settled.set(key, Date.now());
      this.load(core, key, icon)
        .then((done) => {
          this.settled.set(key, done ? null : Date.now());
          if (done) this.loadedIcons.update((n) => n + 1);
        })
        .catch((error) => {
          this.settled.set(key, Date.now());
          console.warn(`Icon ${icon.glyph} could not be loaded`, error);
        });
    }
  }

  async search(query: string, limit = 30): Promise<GlyphSearch> {
    const response = await firstValueFrom(
      this.http.get<{ total: number; items: { glyph: string }[] }>('/api/icons/material-symbols/glyphs', {
        params: { query, limit },
      }),
    );
    return { glyphs: response.items.map((item) => item.glyph), total: response.total };
  }

  /** True when the SVG is registered, or the source has none and the glyph stays a missing box. */
  private async load(core: IconCore, key: string, icon: IconRef): Promise<boolean> {
    try {
      const svg = await firstValueFrom(
        this.http.get(`/api/icons/material-symbols/glyphs/${encodeURIComponent(icon.glyph)}`, {
          params: glyphParams(icon),
          responseType: 'text',
        }),
      );
      const refusal = core.registerIcon(key, svg);
      if (refusal) throw new Error(refusal);
      return true;
    } catch (error) {
      if (error instanceof HttpErrorResponse && error.status === 404) return true;
      throw error;
    }
  }
}
