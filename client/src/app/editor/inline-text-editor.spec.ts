import { TestBed } from '@angular/core/testing';
import { INTER_STACK_ID } from '@canvas/bridge';
import type { NodeProps, TextRun } from '@canvas/bridge';
import { InlineTextEditor } from './inline-text-editor';
const text: TextRun = {
  content: 'Texto original',
  stack: INTER_STACK_ID,
  fontSize: 24,
  lineHeight: 1.4,
  letterSpacing: 1,
  align: 'center',
};
async function setup(bound = false, accept = true) {
  await TestBed.configureTestingModule({ imports: [InlineTextEditor] }).compileComponents();
  const fixture = TestBed.createComponent(InlineTextEditor),
    saved: TextRun[] = [];
  let closed = 0;
  fixture.componentRef.setInput('node', {
    id: 'label',
    type: 'text',
    text,
    textBound: bound,
  } as unknown as NodeProps);
  fixture.componentRef.setInput('save', (_id: string, next: TextRun) => {
    saved.push(next);
    return accept;
  });
  fixture.componentInstance.closed.subscribe(() => closed++);
  await fixture.whenStable();
  const field = fixture.nativeElement.querySelector('textarea') as HTMLTextAreaElement;
  const edit = (value: string) => {
    field.value = value;
    field.dispatchEvent(new Event('input'));
  };
  return { fixture, field, saved, edit, closed: () => closed };
}
describe('text editing on canvas', () => {
  it('uses the actual slant axis and weight, then restores upright editing on face change', async () => {
    const { fixture, field, edit, saved } = await setup();
    for (const [axes, weight, slant] of [
      [{ slnt: -10 }, '400', "'wght' 400, 'slnt' -10"],
      [{ wght: 700, slnt: -10 }, '700', "'wght' 700, 'slnt' -10"],
      [{}, '400', "'wght' 400, 'slnt' 0"],
    ] as const) {
      fixture.componentRef.setInput('node', {
        id: 'label',
        type: 'text',
        text: { ...text, axes },
      } as unknown as NodeProps);
      await fixture.whenStable();
      expect(field.style.fontWeight).toBe(weight);
      expect(field.style.fontVariationSettings).toBe(slant);
    }
    fixture.componentRef.setInput('node', {
      id: 'label',
      type: 'text',
      text: { ...text, axes: { wght: 700, slnt: -10 } },
    } as unknown as NodeProps);
    await fixture.whenStable();
    edit('Inclinado');
    field.dispatchEvent(new Event('blur'));
    expect(saved.at(-1)?.axes).toEqual({ wght: 700, slnt: -10 });
  });
  it('uses the selected bold weight and feature settings and keeps them on save', async () => {
    const { fixture, field, edit, saved } = await setup();
    fixture.componentRef.setInput('node', {
      id: 'label',
      type: 'text',
      text: { ...text, axes: { wght: 700 }, features: { calt: false } },
    } as unknown as NodeProps);
    await fixture.whenStable();
    expect(field.style.fontWeight).toBe('700');
    expect(field.style.fontFeatureSettings).toBe("'calt' 0");
    edit('Texto negrita');
    field.dispatchEvent(new Event('blur'));
    expect(saved.at(-1)?.axes).toEqual({ wght: 700 });
  });
  it('commits normalized multiline content once and preserves typography', async () => {
    const { field, edit, saved, closed } = await setup();
    edit('Disen\u0303o\nJose\u0301');
    field.dispatchEvent(
      new KeyboardEvent('keydown', { key: 'Enter', ctrlKey: true, bubbles: true }),
    );
    field.dispatchEvent(new Event('blur'));
    expect(saved).toEqual([{ ...text, content: 'Diseño\nJosé' }]);
    expect(closed()).toBe(1);
  });
  it('cancels without creating an operation even if destruction blurs the control', async () => {
    const { field, edit, saved, closed } = await setup();
    edit('Borrador');
    field.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true }));
    field.dispatchEvent(new Event('blur'));
    expect(saved).toEqual([]);
    expect(closed()).toBe(1);
  });
  it('keeps unsupported input and long drafts local without closing', async () => {
    const { fixture, field, edit, saved, closed } = await setup();
    for (const value of ['a'.repeat(2049), 'a\t', 'a\u0085', 'a\u007f']) {
      edit(value);
      field.dispatchEvent(new Event('blur'));
      await fixture.whenStable();
      expect(fixture.nativeElement.querySelector('[role=alert]')).not.toBeNull();
    }
    expect(saved).toEqual([]);
    expect(closed()).toBe(0);
  });
  it('keeps a native-rejected draft available for correction', async () => {
    const { fixture, field, edit, saved, closed } = await setup(false, false);
    edit('Revisar');
    field.dispatchEvent(new Event('blur'));
    await fixture.whenStable();
    expect(saved).toHaveLength(1);
    expect(field.value).toBe('Revisar');
    expect(closed()).toBe(0);
    expect(fixture.nativeElement.querySelector('[role=alert]').textContent).toContain('refused');
  });
  it('does not bypass a declared text binding', async () => {
    const { fixture, field, saved, closed } = await setup(true);
    expect(field.readOnly).toBe(true);
    field.dispatchEvent(
      new KeyboardEvent('keydown', { key: 'Enter', metaKey: true, bubbles: true }),
    );
    field.dispatchEvent(new Event('blur'));
    expect(saved).toEqual([]);
    expect(closed()).toBe(0);
    expect(fixture.nativeElement.textContent).toContain('declared property');
  });
  it('closes an unchanged edit without generating an undo entry', async () => {
    const { field, saved, closed } = await setup();
    field.dispatchEvent(new Event('blur'));
    expect(saved).toEqual([]);
    expect(closed()).toBe(1);
  });
  it('keeps Enter as a new line and does not submit during IME composition', async () => {
    const { field, edit, saved, closed } = await setup();
    edit('Diseño');
    field.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', bubbles: true }));
    field.dispatchEvent(
      new KeyboardEvent('keydown', {
        key: 'Enter',
        ctrlKey: true,
        isComposing: true,
        bubbles: true,
      }),
    );
    expect(saved).toEqual([]);
    expect(closed()).toBe(0);
  });
});
