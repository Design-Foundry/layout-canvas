import { HttpClient, type HttpResponse } from '@angular/common/http';
import { Injectable, Injector, inject, signal } from '@angular/core';
import { firstValueFrom } from 'rxjs';
import { INTER_STACK_ID } from '@canvas/bridge';
import type { Font, FontStack, GenericFallback, VisualCore } from '@canvas/bridge';

export const GOOGLE_PROVIDER = 'googlefonts';

export interface FontsSnapshot {
  readonly fonts: readonly Font[];
  readonly fontStacks: readonly FontStack[];
}
export interface FontManifestFile {
  readonly face: string;
  readonly fixed?: Record<string, number>;
  readonly sha256: string;
  readonly file: string;
}
export interface PinnedFont {
  readonly font: Omit<Font, 'id' | 'order'>;
  readonly genericFallback: GenericFallback;
}
type FontCore = Pick<
  VisualCore,
  'fonts' | 'addFont' | 'addFontStack' | 'registerFontFamily' | 'registerFontFace'
>;

/** The order key that sorts after `last`, following the document's fractional index format. */
export function orderAfter(last: string | undefined): string {
  if (!last) return 'a0';
  const digits = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz';
  const head = last[0];
  const length =
    head >= 'a' && head <= 'z' ? head.charCodeAt(0) - 97 + 2 : 'Z'.charCodeAt(0) - head.charCodeAt(0) + 2;
  const tail = [...last.slice(1, length)];
  for (let i = tail.length - 1; i >= 0; i--) {
    const next = digits.indexOf(tail[i]) + 1;
    if (next < digits.length) {
      tail[i] = digits[next];
      return head + tail.join('');
    }
    tail[i] = '0';
  }
  if (head === 'Z') return 'a0';
  if (head === 'z') return last + 'V';
  const nextHead = String.fromCharCode(head.charCodeAt(0) + 1);
  return nextHead > 'a' ? nextHead + tail.join('') + '0' : nextHead + tail.slice(1).join('');
}

@Injectable({ providedIn: 'root' })
export class FontService {
  private readonly injector = inject(Injector);
  private readonly loaded = new Set<string>();
  private core: FontCore | undefined;

  readonly snapshot = signal<FontsSnapshot>({ fonts: [], fontStacks: [] });

  private get http(): HttpClient {
    return this.injector.get(HttpClient);
  }

  attach(core: FontCore | undefined): void {
    if (core !== this.core) this.loaded.clear();
    this.core = core;
    if (core) this.refresh();
  }

  /** Reads the document's fonts and starts loading the families that are not loaded yet. */
  refresh(): void {
    if (!this.core) return;
    const next = this.core.fonts();
    if (JSON.stringify(next) !== JSON.stringify(this.snapshot())) this.snapshot.set(next);
    for (const font of next.fonts) {
      if (font.provider !== GOOGLE_PROVIDER || !font.familyVersion) continue;
      const id = `${font.key}@${font.familyVersion}`;
      if (this.loaded.has(id)) continue;
      this.loaded.add(id);
      this.load(font.key, font.familyVersion).catch((error) =>
        console.warn(`Font ${font.key} could not be loaded`, error),
      );
    }
  }

  async search(query: string): Promise<string[]> {
    const response = await firstValueFrom(
      this.http.get<{ families: { family: string }[] }>('/api/fonts/google/families', {
        params: { query, limit: 30 },
      }),
    );
    return response.families.map((entry) => entry.family);
  }

  /**
   * Makes `family` usable by the document: reuses its font and one-font stack when present,
   * otherwise pins it and adds both. Returns the stack id, or null when the edit was refused.
   */
  async use(family: string): Promise<string | null> {
    const core = this.core;
    if (!core) return null;
    const { fonts, fontStacks } = core.fonts();
    const existing = fonts.find((f) => f.provider === GOOGLE_PROVIDER && f.key === family);
    if (existing) {
      return (
        fontStacks.find((s) => s.entries.length === 1 && s.entries[0] === existing.id)?.id ??
        this.addStack(core, existing, 'sans-serif')
      );
    }
    const pinned = await firstValueFrom(
      this.http.post<PinnedFont>('/api/fonts/google/pin', { family }),
    );
    const latest = core.fonts().fonts;
    const font: Font = {
      ...pinned.font,
      id: crypto.randomUUID(),
      order: orderAfter(latest.at(-1)?.order),
    };
    if (!core.addFont(font)) return null;
    this.refresh();
    return this.addStack(core, font, pinned.genericFallback);
  }

  private addStack(core: FontCore, font: Font, genericFallback: GenericFallback): string | null {
    const stack: FontStack = {
      id: crypto.randomUUID(),
      identifier: font.identifier,
      displayName: font.displayName,
      entries: [font.id],
      genericFallback,
      order: orderAfter(core.fonts().fontStacks.at(-1)?.order),
    };
    return core.addFontStack(stack) ? stack.id : null;
  }

  /** The display name of the first font of `stackId`. */
  nameOf(stackId: string): string {
    if (stackId === INTER_STACK_ID) return 'Inter';
    const { fonts, fontStacks } = this.snapshot();
    const stack = fontStacks.find((s) => s.id === stackId);
    const font = fonts.find((f) => f.id === stack?.entries[0]);
    return stack?.displayName ?? font?.displayName ?? font?.identifier ?? stack?.identifier ?? '';
  }

  private async load(key: string, version: string): Promise<void> {
    const core = this.core;
    if (!core) return;
    const base = `/api/fonts/${GOOGLE_PROVIDER}/${encodeURIComponent(version)}/${encodeURIComponent(key)}/files`;
    const files = await firstValueFrom(this.http.get<FontManifestFile[]>(base));
    const refusal = core.registerFontFamily(
      GOOGLE_PROVIDER,
      key,
      version,
      files.map((f) => ({ face: f.face, fixed: f.fixed })),
    );
    if (refusal) throw new Error(refusal);
    await Promise.all(
      files.map(async (f) => {
        try {
          const response: HttpResponse<ArrayBuffer> = await firstValueFrom(
            this.http.get(`${base}/${encodeURIComponent(f.file)}`, {
              observe: 'response',
              responseType: 'arraybuffer',
            }),
          );
          const header = response.headers.get('X-Font-SHA256');
          if (header?.toLowerCase() !== f.sha256.toLowerCase())
            throw new Error('checksum differs from the manifest');
          const failure = core.registerFontFace(f.face, new Uint8Array(response.body!), f.sha256);
          if (failure) throw new Error(failure);
        } catch (error) {
          console.warn(`Font file ${f.file} of ${key} was skipped`, error);
        }
      }),
    );
  }
}
