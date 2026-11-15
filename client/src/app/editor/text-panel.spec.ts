import { signal } from '@angular/core';
import { TestBed } from '@angular/core/testing';
import { INTER_STACK_ID } from '@canvas/bridge';
import type { NodeProps, TextRun } from '@canvas/bridge';
import { FontService } from './font-service';
import { TextPanel } from './text-panel';

const text: TextRun = {
  content: 'Continuar',
  stack: INTER_STACK_ID,
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
    for (const [style, axes] of [
      ['oblique', { slnt: -10 }],
      ['bold-oblique', { wght: 700, slnt: -10 }],
    ] as const) {
      select.value = style;
      select.dispatchEvent(new Event('change'));
      await fixture.whenStable();
      expect(saved.at(-1)?.stack).toBe(INTER_STACK_ID);
      expect(saved.at(-1)?.axes).toEqual(axes);
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
    select.value = 'bold';
    select.dispatchEvent(new Event('change'));
    await fixture.whenStable();
    expect(saved.at(-1)).toEqual({ ...text, axes: { wght: 700 } });
    fixture.componentRef.setInput('save', () => false);
    await fixture.whenStable();
    select.value = 'regular';
    select.dispatchEvent(new Event('change'));
    await fixture.whenStable();
    expect(select.value).toBe('bold');
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
    textarea.value = 'Hola\u0007';
    textarea.dispatchEvent(new Event('input'));
    textarea.dispatchEvent(new Event('blur'));
    await fixture.whenStable();
    expect(calls).toBe(0);
    expect(fixture.nativeElement.querySelector('[role="alert"]').textContent).toContain(
      'unsupported',
    );
  });
  it('accepts Cyrillic, Greek, Arabic, CJK and emoji content', async () => {
    const saved: TextRun[] = [];
    const fixture = await setup((_, next) => {
      saved.push(next);
      return true;
    });
    const textarea = fixture.nativeElement.querySelector('textarea') as HTMLTextAreaElement;
    const samples = ['Привет', 'Γειά σου', 'مرحبا', '你好こんにちは', 'Hola 😀'];
    for (const value of samples) {
      textarea.value = value;
      textarea.dispatchEvent(new Event('input'));
      textarea.dispatchEvent(new Event('blur'));
      await fixture.whenStable();
    }
    expect(saved.map((run) => run.content)).toEqual(samples);
    expect(fixture.nativeElement.querySelector('[role="alert"]')).toBeNull();
  });
  it('keeps the saved style when the native layer refuses a change', async () => {
    const fixture = await setup(() => false);
    (fixture.componentInstance as any).number('fontSize', 40);
    await fixture.whenStable();
    expect((fixture.componentInstance as any).value().fontSize).toBe(24);
    expect(fixture.nativeElement.querySelector('[role="alert"]').textContent).toContain('refused');
  });
});

describe('font family picker', () => {
  const service = {
    snapshot: signal({
      fonts: [],
      fontStacks: [{ id: 'lora-stack', identifier: 'lora', displayName: 'Lora', entries: [] }],
    }),
    nameOf: (id: string) => (id === INTER_STACK_ID ? 'Inter' : id === 'lora-stack' ? 'Lora' : ''),
    search: vi.fn(async (_query: string) => ['Lora', 'Roboto']),
    use: vi.fn(async (_family: string): Promise<string | null> => 'lora-stack'),
  };
  async function pickerSetup(stack = INTER_STACK_ID) {
    TestBed.resetTestingModule();
    TestBed.configureTestingModule({ providers: [{ provide: FontService, useValue: service }] });
    const saved: TextRun[] = [];
    const fixture = TestBed.createComponent(TextPanel);
    fixture.componentRef.setInput('node', {
      id: 'text',
      type: 'text',
      text: { ...text, stack },
    } as NodeProps);
    fixture.componentRef.setInput('save', (_id: string, next: TextRun) => {
      saved.push(next);
      return true;
    });
    await fixture.whenStable();
    return { fixture, saved, el: fixture.nativeElement as HTMLElement };
  }
  const input = (el: HTMLElement) => el.querySelector('[aria-label="Font"]') as HTMLInputElement;
  const option = (el: HTMLElement, name: string) =>
    [...el.querySelectorAll('[role="option"] button')].find(
      (b) => b.textContent?.trim() === name,
    ) as HTMLButtonElement;

  beforeEach(() => {
    service.search.mockClear();
    service.use.mockClear();
  });
  afterEach(() => vi.useRealTimers());
  const settle = () => new Promise((resolve) => setTimeout(resolve));

  it('shows the current font and the style select only for Inter', async () => {
    const inter = await pickerSetup();
    expect(input(inter.el).value).toBe('Inter');
    expect(inter.el.querySelector('[aria-label="Font style"]')).not.toBeNull();
    const other = await pickerSetup('lora-stack');
    expect(input(other.el).value).toBe('Lora');
    expect(other.el.querySelector('[aria-label="Font style"]')).toBeNull();
  });

  it('searches with a debounce and lists the families', async () => {
    const { fixture, el } = await pickerSetup();
    vi.useFakeTimers();
    input(el).dispatchEvent(new Event('focus'));
    input(el).value = 'lo';
    input(el).dispatchEvent(new Event('input'));
    input(el).value = 'lor';
    input(el).dispatchEvent(new Event('input'));
    await vi.advanceTimersByTimeAsync(300);
    vi.useRealTimers();
    await fixture.whenStable();
    expect(service.search.mock.calls.map((call) => call[0])).toEqual(['', 'lor']);
    expect(option(el, 'Lora')).toBeTruthy();
    expect(option(el, 'Roboto')).toBeTruthy();
  });

  it('picks a family through the font service and sets the stack with empty axes', async () => {
    const { fixture, saved, el } = await pickerSetup();
    input(el).dispatchEvent(new Event('focus'));
    await fixture.whenStable();
    option(el, 'Lora').click();
    await settle();
    expect(service.use).toHaveBeenCalledWith('Lora');
    expect(saved.at(-1)).toMatchObject({ stack: 'lora-stack', axes: {} });
  });

  it('keeps the saved font when the family cannot be added', async () => {
    service.use.mockResolvedValueOnce(null);
    const { fixture, saved, el } = await pickerSetup();
    input(el).dispatchEvent(new Event('focus'));
    await fixture.whenStable();
    option(el, 'Lora').click();
    await settle();
    await fixture.whenStable();
    expect(saved).toEqual([]);
    expect(el.querySelector('[role="alert"]')).not.toBeNull();
  });

  it('picks Inter without the font service', async () => {
    const { fixture, saved, el } = await pickerSetup('lora-stack');
    input(el).dispatchEvent(new Event('focus'));
    await fixture.whenStable();
    option(el, 'Inter').click();
    expect(service.use).not.toHaveBeenCalled();
    expect(saved.at(-1)).toMatchObject({ stack: INTER_STACK_ID, axes: {} });
  });

  it('disables the picker for a shared typography', async () => {
    const { fixture, el } = await pickerSetup();
    fixture.componentRef.setInput('node', {
      id: 'text',
      type: 'text',
      text,
      typography: 'shared',
    } as NodeProps);
    await fixture.whenStable();
    expect(input(el).disabled).toBe(true);
  });
});
