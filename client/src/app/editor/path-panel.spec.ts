import { TestBed } from '@angular/core/testing';
import { PathPanel, normalizePath } from './path-panel';
import type { NodeProps, NodePatch } from '@canvas/bridge';

const source = {
  id: 'path',
  type: 'path',
  x: 20,
  y: 30,
  w: 100,
  h: 80,
  lineWidth: 2,
  path: {
    closed: false,
    points: [
      { x: 0, y: 0 },
      { x: 1, y: 1 },
      { x: 0, y: 1 },
    ],
  },
} as unknown as NodeProps;
async function setup(accept = true) {
  await TestBed.configureTestingModule({ imports: [PathPanel] }).compileComponents();
  const fixture = TestBed.createComponent(PathPanel),
    saved: NodePatch[] = [];
  fixture.componentRef.setInput('node', source);
  fixture.componentRef.setInput('save', (_id: string, patch: NodePatch) => {
    saved.push(patch);
    return accept;
  });
  await fixture.whenStable();
  const change = async (label: string, value: string) => {
    const element = [...fixture.nativeElement.querySelectorAll('app-number-field')].find(
      (field: any) => field.textContent.includes(label),
    ) as HTMLElement;
    const input = element.querySelector('input')!;
    input.dispatchEvent(new FocusEvent('focusin', { bubbles: true }));
    input.value = value;
    input.dispatchEvent(new Event('input', { bubbles: true }));
    input.dispatchEvent(new FocusEvent('focusout', { bubbles: true }));
    await fixture.whenStable();
  };
  const click = async (text: string) => {
    (
      [...fixture.nativeElement.querySelectorAll('button')].find((button: any) =>
        button.textContent.includes(text),
      ) as HTMLButtonElement
    ).click();
    await fixture.whenStable();
  };
  return { fixture, saved, change, click };
}
describe('native path inspector', () => {
  it('normalizes crossing and zero-axis vertices without moving the other vertices', () => {
    expect(
      normalizePath(
        [
          { x: 50, y: 10 },
          { x: 50, y: -10 },
        ],
        false,
      ),
    ).toEqual({
      x: 50,
      y: -10,
      w: 0,
      h: 20,
      path: {
        closed: false,
        points: [
          { x: 0, y: 1 },
          { x: 0, y: 0 },
        ],
      },
    });
  });
  it('edits a vertex and the bounding box atomically, with independent width', async () => {
    const { saved, change } = await setup();
    await change('Vertex X', '200');
    expect(saved.at(-1)).toEqual({
      x: 20,
      y: 30,
      w: 180,
      h: 80,
      path: {
        closed: false,
        points: [
          { x: 1, y: 0 },
          { x: 100 / 180, y: 1 },
          { x: 0, y: 1 },
        ],
      },
    });
    await change('Line width', '12');
    expect(saved.at(-1)).toEqual({ lineWidth: 12 });
  });
  it('adds a midpoint, removes it and preserves closure constraints', async () => {
    const { fixture, saved, click } = await setup();
    await click('Add vertex');
    expect(saved.at(-1)?.path?.points).toEqual([
      { x: 0, y: 0 },
      { x: 0.5, y: 0.5 },
      { x: 1, y: 1 },
      { x: 0, y: 1 },
    ]);
    await click('Remove vertex');
    expect(saved.at(-1)?.path?.points).toEqual(source.path?.points);
    const checkbox = fixture.nativeElement.querySelector(
      'input[type=checkbox]',
    ) as HTMLInputElement;
    checkbox.click();
    await fixture.whenStable();
    expect(saved.at(-1)?.path?.closed).toBe(true);
    expect(
      (
        [...fixture.nativeElement.querySelectorAll('button')].find((button: any) =>
          button.textContent.includes('Remove vertex'),
        ) as HTMLButtonElement
      ).disabled,
    ).toBe(true);
  });
  it('keeps refused edits out of pending geometry', async () => {
    const { fixture, saved, change } = await setup(false);
    await change('Vertex X', '200');
    await change('Vertex Y', '0');
    expect(saved.at(-1)?.x).toBe(20);
    expect(saved.at(-1)?.w).toBe(100);
    expect(fixture.nativeElement.querySelector('[role=alert]').textContent).toContain('refused');
  });
  it('locks geometry in auto layout while leaving line width editable for copied paths', async () => {
    const { fixture, saved, change } = await setup();
    fixture.componentRef.setInput('node', { ...source, inFlow: true, inInstance: true });
    await fixture.whenStable();
    expect(fixture.nativeElement.querySelector('fieldset').disabled).toBe(true);
    expect(fixture.nativeElement.textContent).toContain('instance overrides');
    await change('Line width', '8');
    expect(saved.at(-1)).toEqual({ lineWidth: 8 });
  });
  it('includes control extents in normalization without clipping their coordinates', () => {
    const patch = normalizePath(
      [
        { x: 0, y: 0 },
        { x: 100, y: 0 },
      ],
      false,
      [
        { x: -20, y: -40 },
        { x: 80, y: 20 },
      ],
      [
        { x: 20, y: 40 },
        { x: 120, y: -20 },
      ],
    );
    expect([patch.x, patch.y, patch.w, patch.h]).toEqual([-20, -40, 140, 80]);
    expect(patch.path?.inHandles?.[0]).toEqual({ x: 0, y: 0 });
    expect(patch.path?.outHandles?.[1]).toEqual({ x: 1, y: 0.25 });
  });
  it('translates anchor controls and edits control coordinates without moving other anchors', async () => {
    const { fixture, saved, change } = await setup();
    fixture.componentRef.setInput('node', {
      ...source,
      path: {
        points: [
          { x: 0, y: 0.5 },
          { x: 1, y: 0.5 },
        ],
        closed: false,
        inHandles: [
          { x: 0, y: 0.5 },
          { x: 1, y: 0 },
        ],
        outHandles: [
          { x: 0, y: 0 },
          { x: 1, y: 0.5 },
        ],
      },
    });
    await fixture.whenStable();
    await change('Vertex X', '0');
    const patch = saved.at(-1)!,
      path = patch.path!;
    expect(patch.x! + path.outHandles![0].x * patch.w!).toBe(0);
    expect(patch.x! + path.points[1].x * patch.w!).toBe(120);
    await change('Outgoing Y', '-50');
    const next = saved.at(-1)!;
    expect(next.y! + next.path!.outHandles![0].y * next.h!).toBe(-50);
    expect(next.y! + next.path!.points[1].y * next.h!).toBe(70);
  });
  it('splits a cubic segment at its actual midpoint while retaining control arrays', async () => {
    const { fixture, saved, click } = await setup();
    fixture.componentRef.setInput('node', {
      ...source,
      path: {
        points: [
          { x: 0, y: 0.5 },
          { x: 1, y: 0.5 },
        ],
        closed: false,
        inHandles: [
          { x: 0, y: 0.5 },
          { x: 1, y: 0 },
        ],
        outHandles: [
          { x: 0, y: 0 },
          { x: 1, y: 0.5 },
        ],
      },
    });
    await fixture.whenStable();
    await click('Add vertex');
    const patch = saved.at(-1)!,
      p = patch.path!;
    expect(p.points).toHaveLength(3);
    expect(p.inHandles).toHaveLength(3);
    expect(p.outHandles).toHaveLength(3);
    expect(patch.x! + p.points[1].x * patch.w!).toBe(70);
    expect(patch.y! + p.points[1].y * patch.h!).toBe(40);
    expect(patch.x! + p.outHandles![0].x * patch.w!).toBe(20);
    expect(patch.y! + p.outHandles![0].y * patch.h!).toBe(50);
  });
  it('creates real curve controls and removes them again for an all-corner path', async () => {
    const { fixture, saved } = await setup();
    const select = fixture.nativeElement.querySelector(
      'select[aria-label="Vertex type"]',
    ) as HTMLSelectElement;
    select.value = 'curve';
    select.dispatchEvent(new Event('change', { bubbles: true }));
    await fixture.whenStable();
    expect(saved.at(-1)?.path?.inHandles).toHaveLength(3);
    expect(fixture.nativeElement.textContent).toContain('Incoming X');
    select.value = 'corner';
    select.dispatchEvent(new Event('change', { bubbles: true }));
    await fixture.whenStable();
    expect(saved.at(-1)?.path?.inHandles).toBeUndefined();
  });
  it('opening a filled shape clears fill while line width edits retain the path untouched', async () => {
    const { fixture, saved, change } = await setup();
    fixture.componentRef.setInput('node', {
      ...source,
      path: { ...source.path!, closed: true, filled: true },
    });
    await fixture.whenStable();
    await change('Line width', '9');
    expect(saved.at(-1)).toEqual({ lineWidth: 9 });
    const checkbox = fixture.nativeElement.querySelector(
      'fieldset input[type=checkbox]',
    ) as HTMLInputElement;
    checkbox.click();
    await fixture.whenStable();
    expect(saved.at(-1)?.path?.closed).toBe(false);
    expect(saved.at(-1)?.path?.filled ?? false).toBe(false);
    expect(
      (fixture.nativeElement.querySelector('input[aria-label="Filled path"]') as HTMLInputElement)
        .disabled,
    ).toBe(true);
  });
});
