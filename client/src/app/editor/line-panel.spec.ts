import { TestBed } from '@angular/core/testing';
import { LinePanel } from './line-panel';
import type { NodeProps, NodePatch } from '@canvas/bridge';

async function setup(accept = true) {
  await TestBed.configureTestingModule({ imports: [LinePanel] }).compileComponents();
  const fixture = TestBed.createComponent(LinePanel),
    saved: NodePatch[] = [];
  fixture.componentRef.setInput('node', {
    id: 'line',
    type: 'line',
    x: 20,
    y: 30,
    w: 100,
    h: 80,
    lineWidth: 2,
    lineReverse: false,
  } as NodeProps);
  fixture.componentRef.setInput('save', (_id: string, patch: NodePatch) => {
    saved.push(patch);
    return accept;
  });
  await fixture.whenStable();
  const change = async (label: string, value: string) => {
    const fields = [...fixture.nativeElement.querySelectorAll('app-number-field')];
    const element = fields.find((field: any) => field.textContent.includes(label)) as HTMLElement;
    const input = element.querySelector('input')!;
    input.dispatchEvent(new FocusEvent('focusin', { bubbles: true }));
    input.value = value;
    input.dispatchEvent(new Event('input', { bubbles: true }));
    input.dispatchEvent(new FocusEvent('focusout', { bubbles: true }));
    await fixture.whenStable();
  };
  return { fixture, saved, change };
}
describe('native line inspector', () => {
  it('normalizes a crossing endpoint atomically and changes width independently', async () => {
    const { saved, change } = await setup();
    await change('End X', '0');
    expect(saved.at(-1)).toEqual({ x: 0, y: 30, w: 20, h: 80, lineReverse: true });
    await change('Line width', '12');
    expect(saved.at(-1)).toEqual({ lineWidth: 12 });
  });
  it('keeps refused edits out of stored geometry and locks endpoints positioned by auto layout', async () => {
    const { fixture, saved, change } = await setup(false);
    await change('End Y', '150');
    expect(fixture.nativeElement.querySelector('[role=alert]').textContent).toContain('refused');
    expect(saved).toHaveLength(1);
    fixture.componentRef.setInput('node', {
      id: 'line',
      type: 'line',
      x: 20,
      y: 30,
      w: 100,
      h: 80,
      lineWidth: 2,
      inFlow: true,
    } as NodeProps);
    await fixture.whenStable();
    expect(fixture.nativeElement.querySelector('fieldset').disabled).toBe(true);
    expect(fixture.nativeElement.textContent).toContain('follow auto layout');
  });
});
