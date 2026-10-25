import { vi } from 'vitest';
import { EditorPage } from './editor-page';

const route = (EditorPage.prototype as unknown as { onKeydown(event: KeyboardEvent): void })
  .onKeydown;
function setup(drawingPath = false, active = false) {
  const core = {
    drawingPath: () => drawingPath,
    cancelGesture: vi.fn(),
    cancelPath: vi.fn(),
    finishPath: vi.fn(),
    popPathPoint: vi.fn(),
  };
  const page = {
    core: () => core,
    selectedNode: () => null,
    beginInlineText: vi.fn(),
    presentation: () => ({ active }),
    exitPrototype: vi.fn(),
    backPrototype: vi.fn(),
  };
  const key = (
    target: HTMLElement | null = null,
    prevented = false,
    init: KeyboardEventInit = {},
  ) => {
    const event = new KeyboardEvent('keydown', { key: 'Escape', cancelable: true, ...init });
    Object.defineProperty(event, 'target', { value: target });
    if (prevented) event.preventDefault();
    route.call(page, event);
    return event;
  };
  return { core, key, page };
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
  it('exits presentation with Escape before authoring gesture cancellation', () => {
    const { core, key, page } = setup(true, true);
    key();
    expect(page.exitPrototype).toHaveBeenCalledOnce();
    expect(core.cancelPath).not.toHaveBeenCalled();
    expect(core.cancelGesture).not.toHaveBeenCalled();
  });
  it('routes Alt+Left to prototype history and blocks editing shortcuts', () => {
    const { key, page, core } = setup(false, true);
    expect(key(null, false, { key: 'ArrowLeft', altKey: true }).defaultPrevented).toBe(true);
    expect(page.backPrototype).toHaveBeenCalledOnce();
    expect(key(null, false, { key: 'z', ctrlKey: true }).defaultPrevented).toBe(true);
    expect(core.cancelGesture).not.toHaveBeenCalled();
  });
  it('preserves presentation button keyboard activation and tab navigation', () => {
    const { key } = setup(false, true);
    expect(key(null, false, { key: 'Tab' }).defaultPrevented).toBe(false);
    expect(key(document.createElement('button'), false, { key: 'Enter' }).defaultPrevented).toBe(
      false,
    );
  });
  it('does not start inline editing during presentation', () => {
    const begin = (EditorPage.prototype as unknown as { beginInlineText(): void }).beginInlineText;
    const core = vi.fn();
    begin.call({ presentation: () => ({ active: true }), core });
    expect(core).not.toHaveBeenCalled();
  });
  it('preserves browser reload and address shortcuts while blocking editor deletion', () => {
    const { key } = setup(false, true);
    expect(key(null, false, { key: 'r', ctrlKey: true }).defaultPrevented).toBe(false);
    expect(key(null, false, { key: 'l', ctrlKey: true }).defaultPrevented).toBe(false);
    expect(key(null, false, { key: 'Delete' }).defaultPrevented).toBe(true);
  });
});
