import { describe, expect, it, vi } from 'vitest';
import { watchPrototypeMotion } from './prototype-motion';

describe('prototype motion preference', () => {
  it('applies the initial preference, follows changes and removes its listener on exit', () => {
    const core = { setPrototypeReducedMotion: vi.fn() };
    const query = {
      matches: true,
      addEventListener: vi.fn(),
      removeEventListener: vi.fn(),
    };
    const close = watchPrototypeMotion(core, query as unknown as MediaQueryList);
    expect(core.setPrototypeReducedMotion).toHaveBeenCalledWith(true);
    const listener = query.addEventListener.mock.calls[0][1];
    listener({ matches: false });
    expect(core.setPrototypeReducedMotion).toHaveBeenLastCalledWith(false);
    close();
    expect(query.removeEventListener).toHaveBeenCalledWith('change', listener);
  });
  it('keeps normal playback when media queries are unavailable', () => {
    const core = { setPrototypeReducedMotion: vi.fn() };
    watchPrototypeMotion(core)();
    expect(core.setPrototypeReducedMotion).toHaveBeenCalledExactlyOnceWith(false);
  });
});
