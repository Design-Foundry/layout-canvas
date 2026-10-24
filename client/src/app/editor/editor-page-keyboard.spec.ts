import { vi } from 'vitest';
import { EditorPage } from './editor-page';

const route = (EditorPage.prototype as unknown as { onKeydown(event: KeyboardEvent): void })
  .onKeydown;
function setup(drawingPath = false) {
  const core = {
    drawingPath: () => drawingPath,
    cancelGesture: vi.fn(),
    cancelPath: vi.fn(),
    finishPath: vi.fn(),
    popPathPoint: vi.fn(),
  };
  const page = { core: () => core, selectedNode: () => null, beginInlineText: vi.fn() };
  const key = (target: HTMLElement | null = null, prevented = false) => {
    const event = new KeyboardEvent('keydown', { key: 'Escape', cancelable: true });
    Object.defineProperty(event, 'target', { value: target });
    if (prevented) event.preventDefault();
    route.call(page, event);
    return event;
  };
  return { core, key };
}
describe('editor Escape routing', () => {
  it('cancels a generic pointer gesture and consumes the shortcut', () => {
    const { core, key } = setup();
    const event = key();
    expect(core.cancelGesture).toHaveBeenCalledOnce();
    expect(core.cancelPath).not.toHaveBeenCalled();
    expect(event.defaultPrevented).toBe(true);
  });
  it('keeps a pending Pen gesture on its specific cancellation path', () => {
    const { core, key } = setup(true);
    key();
    expect(core.cancelPath).toHaveBeenCalledOnce();
    expect(core.cancelGesture).not.toHaveBeenCalled();
  });
  it('leaves Escape to typing controls, including inline text editing', () => {
    const { core, key } = setup();
    for (const name of ['input', 'textarea', 'select'])
      expect(key(document.createElement(name)).defaultPrevented).toBe(false);
    expect(core.cancelGesture).not.toHaveBeenCalled();
    expect(core.cancelPath).not.toHaveBeenCalled();
  });
  it('respects an Escape already handled by a focused control', () => {
    const { core, key } = setup();
    key(null, true);
    expect(core.cancelGesture).not.toHaveBeenCalled();
  });
});
