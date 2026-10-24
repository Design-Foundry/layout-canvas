import { TestBed } from '@angular/core/testing';
import type { NodeProps, TextRun } from '@canvas/bridge';
import { TextPanel } from './text-panel';

const text: TextRun = {
  content: 'Continuar',
  font: 'inter-regular-v1',
  fontSize: 24,
  lineHeight: 1.4,
  letterSpacing: 0,
  align: 'left',
};
async function setup(save: (id: string, value: TextRun) => boolean) {
  await TestBed.configureTestingModule({ imports: [TextPanel] }).compileComponents();
  const fixture = TestBed.createComponent(TextPanel);
  fixture.componentRef.setInput('node', { id: 'text', type: 'text', text } as NodeProps);
  fixture.componentRef.setInput('save', save);
  await fixture.whenStable();
  return fixture;
}

describe('native text inspector', () => {
  it('binds shared typography, rolls back refusal and detaches with resolved metrics only', async () => {
    const fixture = await setup(() => true),
      patches: any[] = [];
    fixture.componentRef.setInput('tokens', [
      {
        id: 'shared',
        identifier: 'body',
        type: 'typography',
        order: 'a0',
        value: { ...text },
        referenced: false,
      },
    ]);
    fixture.componentRef.setInput('saveStyleBinding', (_id: string, patch: any) => {
      patches.push(patch);
      return true;
    });
    await fixture.whenStable();
    const select = fixture.nativeElement.querySelector(
      '[aria-label="Shared typography"]',
    ) as HTMLSelectElement;
    select.value = 'shared';
    select.dispatchEvent(new Event('change'));
    await fixture.whenStable();
    expect(patches.at(-1)).toEqual({ typography: 'shared' });
    fixture.componentRef.setInput('node', {
      id: 'text',
      type: 'text',
      text,
      typography: 'shared',
    } as NodeProps);
    await fixture.whenStable();
    expect(
      (fixture.nativeElement.querySelector('[aria-label="Font style"]') as HTMLSelectElement)
        .disabled,
    ).toBe(true);
    fixture.componentRef.setInput('saveStyleBinding', () => false);
    await fixture.whenStable();
    select.value = '';
    select.dispatchEvent(new Event('change'));
    await fixture.whenStable();
    expect(select.value).toBe('shared');
    fixture.componentRef.setInput('saveStyleBinding', (_id: string, patch: any) => {
      patches.push(patch);
      return true;
    });
    await fixture.whenStable();
    const detach = [...fixture.nativeElement.querySelectorAll('button')].find((button: any) =>
      button.textContent.includes('Detach typography'),
    ) as HTMLButtonElement;
    detach.click();
    await fixture.whenStable();
    const { content: _content, ...textStyle } = text;
    expect(patches.at(-1)).toEqual({
      typography: '00000000-0000-0000-0000-000000000000',
      textStyle,
    });
  });
  it('keeps source binding readonly on a copied layer while allowing its own typography fields', async () => {
    const fixture = await setup(() => true);
    fixture.componentRef.setInput('node', {
      id: 'copy',
      type: 'text',
      text,
      typography: 'shared',
      inInstance: true,
      textBound: true,
    } as NodeProps);
    await fixture.whenStable();
    expect(fixture.nativeElement.querySelector('[aria-label="Shared typography"]').disabled).toBe(
      true,
    );
    expect(fixture.nativeElement.querySelector('[aria-label="Font style"]').disabled).toBe(false);
    expect(fixture.nativeElement.querySelector('fieldset').disabled).toBe(false);
  });
  it('offers both real oblique faces and saves their native identifiers', async () => {
    const saved: TextRun[] = [];
    const fixture = await setup((_id, next) => {
      saved.push(next);
      return true;
    });
    const select = fixture.nativeElement.querySelector(
      '[aria-label="Font style"]',
    ) as HTMLSelectElement;
    for (const font of ['inter-oblique-v1', 'inter-bold-oblique-v1']) {
      select.value = font;
      select.dispatchEvent(new Event('change'));
      await fixture.whenStable();
      expect(saved.at(-1)?.font).toBe(font);
    }
  });
  it('selects the real face on bound text and restores a native-rejected font selection', async () => {
    const saved: TextRun[] = [];
    const fixture = await setup((_id, next) => {
      saved.push(next);
      return true;
    });
    fixture.componentRef.setInput('node', {
      id: 'copy',
      type: 'text',
      text,
      textBound: true,
    } as NodeProps);
    await fixture.whenStable();
    const select = fixture.nativeElement.querySelector(
      '[aria-label="Font style"]',
    ) as HTMLSelectElement;
    select.value = 'inter-bold-v1';
    select.dispatchEvent(new Event('change'));
    await fixture.whenStable();
    expect(saved.at(-1)).toEqual({ ...text, font: 'inter-bold-v1' });
    fixture.componentRef.setInput('save', () => false);
    await fixture.whenStable();
    select.value = 'inter-regular-v1';
    select.dispatchEvent(new Event('change'));
    await fixture.whenStable();
    expect(select.value).toBe('inter-bold-v1');
    expect(saved).toHaveLength(1);
  });
  it('saves wrapping separately from bound content and restores a rejected toggle', async () => {
    const fixture = await setup(() => true);
    fixture.componentRef.setInput('node', {
      id: 'copy',
      type: 'text',
      text,
      textBound: true,
      textWrap: false,
    } as NodeProps);
    const changes: boolean[] = [];
    fixture.componentRef.setInput('saveWrap', (_id: string, value: boolean) => {
      changes.push(value);
      return true;
    });
    await fixture.whenStable();
    const toggle = fixture.nativeElement.querySelector(
      '[aria-label="Wrap text to width"]',
    ) as HTMLInputElement;
    toggle.click();
    await fixture.whenStable();
    expect(changes).toEqual([true]);
    fixture.componentRef.setInput('node', {
      id: 'copy',
      type: 'text',
      text,
      textBound: true,
      textWrap: true,
    } as NodeProps);
    fixture.componentRef.setInput('saveWrap', () => false);
    await fixture.whenStable();
    toggle.click();
    await fixture.whenStable();
    expect(toggle.checked).toBe(true);
    expect(fixture.nativeElement.querySelector('[role=alert]').textContent).toContain('refused');
  });
  it('locks property-controlled content while keeping typography editable', async () => {
    const fixture = await setup(() => true);
    fixture.componentRef.setInput('node', {
      id: 'copy',
      type: 'text',
      text,
      textBound: true,
      inInstance: true,
    } as NodeProps);
    await fixture.whenStable();
    expect(fixture.nativeElement.querySelector('textarea').readOnly).toBe(true);
    expect(fixture.nativeElement.querySelector('fieldset:disabled')).toBeNull();
    expect(fixture.nativeElement.textContent).toContain('declared property');
  });
  it('normalizes equivalent Spanish accents before native validation', async () => {
    const saved: TextRun[] = [];
    const fixture = await setup((_, next) => {
      saved.push(next);
      return true;
    });
    const textarea = fixture.nativeElement.querySelector('textarea') as HTMLTextAreaElement;
    textarea.value = 'Jose\u0301';
    textarea.dispatchEvent(new Event('input'));
    textarea.dispatchEvent(new Event('blur'));
    expect(saved[0].content).toBe('José');
  });
  it('saves Spanish text and merges rapid style edits before the native refresh', async () => {
    const saved: TextRun[] = [];
    const fixture = await setup((_, next) => {
      saved.push(next);
      return true;
    });
    const textarea = fixture.nativeElement.querySelector('textarea') as HTMLTextAreaElement;
    textarea.value = 'Diseño: José';
    textarea.dispatchEvent(new Event('input'));
    textarea.dispatchEvent(new Event('blur'));
    (fixture.componentInstance as any).number('fontSize', 32);
    (fixture.componentInstance as any).change({ align: 'center' });
    expect(saved.at(-1)).toEqual({
      ...text,
      content: 'Diseño: José',
      fontSize: 32,
      align: 'center',
    });
  });
  it('refuses unsupported characters without invoking the native save', async () => {
    let calls = 0;
    const fixture = await setup(() => {
      ++calls;
      return true;
    });
    const textarea = fixture.nativeElement.querySelector('textarea') as HTMLTextAreaElement;
    textarea.value = 'Hola 😀';
    textarea.dispatchEvent(new Event('input'));
    textarea.dispatchEvent(new Event('blur'));
    await fixture.whenStable();
    expect(calls).toBe(0);
    expect(fixture.nativeElement.querySelector('[role="alert"]').textContent).toContain(
      'unsupported',
    );
  });
  it('keeps the saved style when the native layer refuses a change', async () => {
    const fixture = await setup(() => false);
    (fixture.componentInstance as any).number('fontSize', 40);
    await fixture.whenStable();
    expect((fixture.componentInstance as any).value().fontSize).toBe(24);
    expect(fixture.nativeElement.querySelector('[role="alert"]').textContent).toContain('refused');
  });
});
