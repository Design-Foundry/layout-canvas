import { vi } from 'vitest';
import { INTER_FONT_ID, INTER_STACK_ID } from '@canvas/bridge';
import type { Font, FontStack } from '@canvas/bridge';

export function font(id: string, tag: Partial<Font['capabilities']> = {}, extra: Partial<Font> = {}): Font {
  return {
    id,
    identifier: id,
    provider: 'googlefonts',
    key: id,
    capabilities: { axes: [], features: [], ...tag },
    order: 'a0',
    ...extra,
  };
}

export const INTER = font(
  INTER_FONT_ID,
  {
    axes: [
      { tag: 'wght', domain: { min: 100, max: 900 }, default: 400 },
      { tag: 'slnt', domain: { min: -10, max: 0 }, default: 0 },
    ],
    features: ['kern', 'liga', 'tnum'],
  },
  { provider: 'builtin', displayName: 'Inter' },
);

export function stackOf(id: string, fontId: string, genericFallback: FontStack['genericFallback'] = 'sans-serif'): FontStack {
  return { id, identifier: id, entries: [fontId], genericFallback, order: 'a0' };
}

export function fontCore(fonts: Font[] = [INTER], stacks: FontStack[] = [stackOf(INTER_STACK_ID, INTER_FONT_ID)]) {
  const off = new Set<string>();
  return {
    fonts: vi.fn(() => ({ fonts, fontStacks: stacks })),
    addFont: vi.fn(),
    addFontStack: vi.fn(),
    registerFontFamily: vi.fn(),
    registerFontFace: vi.fn(),
    textFallback: vi.fn(
      (_id: string): { approximated: boolean; missing: boolean; generic: string | null } | null => null,
    ),
    setFontAvailable: vi.fn((id: string, available: boolean) => {
      if (available) off.delete(id);
      else off.add(id);
      return true;
    }),
    unavailableFonts: vi.fn(() => [...off]),
  };
}
