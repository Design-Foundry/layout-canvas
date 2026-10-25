import { VisualCore } from '@canvas/bridge';

const surface = '--color--semantic--surface-default';

function resolve(): [number, number, number, number] | null {
  const probe = document.createElement('div');
  probe.style.background = `var(${surface})`;
  document.body.appendChild(probe);
  const css = getComputedStyle(probe).backgroundColor;
  probe.remove();
  const context = document.createElement('canvas').getContext('2d', { willReadFrequently: true });
  if (!context || !css) return null;
  context.clearRect(0, 0, 1, 1);
  context.fillStyle = css;
  context.fillRect(0, 0, 1, 1);
  const [r, g, b, a] = context.getImageData(0, 0, 1, 1).data;
  return [r / 255, g / 255, b / 255, a / 255];
}

/** Paints the canvas with the page surface and keeps it in step with the theme. Returns a disposer. */
export function followTheme(core: VisualCore): () => void {
  const apply = () => {
    const color = resolve();
    if (color) core.setCanvasBackground(...color);
  };
  apply();
  const query = matchMedia('(prefers-color-scheme: dark)');
  const observer = new MutationObserver(apply);
  observer.observe(document.documentElement, { attributes: true, attributeFilter: ['data-theme'] });
  query.addEventListener('change', apply);
  return () => {
    observer.disconnect();
    query.removeEventListener('change', apply);
  };
}
