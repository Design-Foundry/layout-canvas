import type { VisualCore } from '@canvas/bridge';

/** A playback preference, never authored document metadata. */
export function watchPrototypeMotion(
  core: Pick<VisualCore, 'setPrototypeReducedMotion'>,
  query?: MediaQueryList,
): () => void {
  core.setPrototypeReducedMotion(query?.matches ?? false);
  if (!query) return () => {};
  const changed = (event: MediaQueryListEvent) => core.setPrototypeReducedMotion(event.matches);
  query.addEventListener('change', changed);
  return () => query.removeEventListener('change', changed);
}
