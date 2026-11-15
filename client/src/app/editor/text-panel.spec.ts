import { signal } from '@angular/core';
import { TestBed } from '@angular/core/testing';
import { INTER_STACK_ID } from '@canvas/bridge';
import type { NodeProps, TextRun } from '@canvas/bridge';
import { FontService } from './font-service';
import { INTER, font, fontCore, stackOf } from './font-core.testing';
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
  TestBed.inject(FontService).attach(fontCore() as any);
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
      (fixture.nativeElement.querySelector('[aria-label="Weight"]') as HTMLSelectElement)
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
    expect(fixture.nativeElement.querySelector('[aria-label="Weight"]').disabled).toBe(false);
    expect(fixture.nativeElement.querySelector('fieldset').disabled).toBe(false);
  });
  it('reproduces the four Inter styles with weight and italic', async () => {
    const saved: TextRun[] = [];
    const fixture = await setup((_id, next) => {
      saved.push(next);
      return true;
    });
    const el = fixture.nativeElement as HTMLElement;
    const select = el.querySelector('[aria-label="Weight"]') as HTMLSelectElement;
    const italic = el.querySelector('[aria-label="Italic"]') as HTMLInputElement;
    italic.click();
    await fixture.whenStable();
    expect(saved.at(-1)).toEqual({ ...text, axes: { slnt: -10 }, features: {} });
    select.value = '700';
    select.dispatchEvent(new Event('change'));
    await fixture.whenStable();
    expect(saved.at(-1)).toEqual({ ...text, axes: { slnt: -10, wght: 700 }, features: {} });
    italic.click();
    await fixture.whenStable();
    expect(saved.at(-1)?.axes).toEqual({ slnt: 0, wght: 700 });
  });
  it('sends stack, axes and features together and restores a refused weight', async () => {
    const saved: TextRun[] = [];
    const fixture = await setup((_id, next) => {
      saved.push(next);
      return true;
    });
    const select = fixture.nativeElement.querySelector('[aria-label="Weight"]') as HTMLSelectElement;
    select.value = '700';
    select.dispatchEvent(new Event('change'));
    await fixture.whenStable();
    expect(saved.at(-1)).toEqual({ ...text, axes: { wght: 700 }, features: {} });
    fixture.componentRef.setInput('save', () => false);
    await fixture.whenStable();
    select.value = '300';
    select.dispatchEvent(new Event('change'));
    await fixture.whenStable();
    expect(select.value).toBe('700');
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
    unavailable: signal<string[]>([]),
    loadedFamilies: signal(0),
    fallbackOf: () => null,
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

  it('shows the current font', async () => {
    const inter = await pickerSetup();
    expect(input(inter.el).value).toBe('Inter');
    const other = await pickerSetup('lora-stack');
    expect(input(other.el).value).toBe('Lora');
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

describe('font controls and fallback cue', () => {
  const lora = font('lora', {
    axes: [
      { tag: 'wght', domain: { values: [400, 700] }, default: 400 },
      { tag: 'ital', domain: { values: [0, 1] }, default: 0 },
      { tag: 'wdth', domain: { min: 75, max: 125 }, default: 100 },
      { tag: 'GRAD', domain: { values: [0, 50] } },
      { tag: 'XTRA', domain: { min: 400, max: 600 } },
    ],
    features: ['kern', 'liga', 'tnum', 'ss03', 'cv12', 'zzzz'],
  });
  const variable = font('var', {
    axes: [{ tag: 'wght', domain: { min: 250, max: 650 }, default: 450 }],
  });
  const core = fontCore(
    [INTER, lora, variable],
    [
      stackOf(INTER_STACK_ID, INTER.id),
      stackOf('lora-stack', 'lora', 'serif'),
      stackOf('var-stack', 'var'),
    ],
  );
  const saved: TextRun[] = [];
  async function setup(stack: string, run: Partial<TextRun> = {}, styleOnly = false) {
    TestBed.resetTestingModule();
    TestBed.configureTestingModule({});
    const service = TestBed.inject(FontService);
    service.attach(core as any);
    saved.length = 0;
    const fixture = TestBed.createComponent(TextPanel);
    fixture.componentRef.setInput('node', {
      id: 'text',
      type: 'text',
      text: { ...text, stack, ...run },
    } as NodeProps);
    fixture.componentRef.setInput('styleOnly', styleOnly);
    fixture.componentRef.setInput('save', (_id: string, next: TextRun) => {
      saved.push(next);
      return true;
    });
    await fixture.whenStable();
    return { fixture, service, el: fixture.nativeElement as HTMLElement };
  }
  const q = <T extends Element>(el: HTMLElement, selector: string) => el.querySelector(selector) as T;
  const labels = (el: HTMLElement, selector: string) =>
    [...el.querySelectorAll(`${selector} option`)].map((o) => o.textContent?.trim());

  beforeEach(() => {
    core.textFallback.mockReset();
    core.textFallback.mockReturnValue(null);
  });

  it('lists discrete weights with their names', async () => {
    const { el } = await setup('lora-stack');
    expect(labels(el, '[aria-label="Weight"]')).toEqual(['400 Regular', '700 Bold']);
  });

  it('lists the standard stops inside a continuous domain plus the current value', async () => {
    const { el } = await setup('var-stack', { axes: { wght: 333 } });
    expect(labels(el, '[aria-label="Weight"]')).toEqual([
      '300 Light',
      '333',
      '400 Regular',
      '500 Medium',
      '600 Semi Bold',
    ]);
    expect(q<HTMLSelectElement>(el, '[aria-label="Weight"]').value).toBe('333');
    const inter = await setup(INTER_STACK_ID);
    expect(labels(inter.el, '[aria-label="Weight"]')).toHaveLength(9);
  });

  it('sets ital to 1 or 0 when the font has an ital axis', async () => {
    const { fixture, el } = await setup('lora-stack');
    q<HTMLInputElement>(el, '[aria-label="Italic"]').click();
    await fixture.whenStable();
    expect(saved.at(-1)).toMatchObject({ stack: 'lora-stack', axes: { ital: 1 } });
    q<HTMLInputElement>(el, '[aria-label="Italic"]').click();
    await fixture.whenStable();
    expect(saved.at(-1)?.axes).toEqual({ ital: 0 });
  });

  it('has no italic without ital or a negative slnt, and no weight without wght', async () => {
    const { el } = await setup('var-stack');
    expect(q(el, '[aria-label="Italic"]')).toBeNull();
    const none = fontCore([font('plain')], [stackOf('plain-stack', 'plain')]);
    TestBed.inject(FontService).attach(none as any);
    const fixture = TestBed.createComponent(TextPanel);
    fixture.componentRef.setInput('node', {
      id: 'text',
      type: 'text',
      text: { ...text, stack: 'plain-stack' },
    } as NodeProps);
    fixture.componentRef.setInput('save', () => true);
    await fixture.whenStable();
    expect(q(fixture.nativeElement, '[aria-label="Weight"]')).toBeNull();
    expect(q(fixture.nativeElement, 'details')).toBeNull();
  });

  it('limits other axes to their domains', async () => {
    const { fixture, el } = await setup('lora-stack');
    const width = q<HTMLInputElement>(el, '[aria-label="Width"], app-number-field input');
    expect(width).not.toBeNull();
    const grad = q<HTMLSelectElement>(el, 'select[aria-label="GRAD"]');
    expect(labels(el, 'select[aria-label="GRAD"]')).toEqual(['0', '50']);
    grad.value = '50';
    grad.dispatchEvent(new Event('change'));
    await fixture.whenStable();
    expect(saved.at(-1)?.axes).toEqual({ GRAD: 50 });
    const field = [...el.querySelectorAll('app-number-field')].find((n) =>
      n.textContent?.includes('Width'),
    )!;
    const inner = field.querySelector('input') as HTMLInputElement;
    inner.dispatchEvent(new FocusEvent('focusin', { bubbles: true }));
    inner.value = '300';
    inner.dispatchEvent(new Event('input', { bubbles: true }));
    inner.dispatchEvent(new FocusEvent('focusout', { bubbles: true }));
    await fixture.whenStable();
    expect(saved.at(-1)?.axes).toEqual({ GRAD: 50, wdth: 125 });
    expect([...el.querySelectorAll('app-number-field')].some((n) => n.textContent?.includes('XTRA'))).toBe(true);
  });

  it('names features and keeps default-on features checked', async () => {
    const { el } = await setup('lora-stack');
    const names = [...el.querySelectorAll('details label')].map((l) => l.textContent?.trim());
    expect(names).toEqual([
      'Kerning',
      'Ligatures',
      'Tabular figures',
      'Stylistic set 3',
      'Character variant 12',
      'zzzz',
    ]);
    const box = (name: string) => q<HTMLInputElement>(el, `[aria-label="${name}"]`);
    expect(box('Kerning').checked).toBe(true);
    expect(box('Tabular figures').checked).toBe(false);
  });

  it('sets a feature true, removes it, and writes false for default-on features', async () => {
    const { fixture, el } = await setup('lora-stack', { axes: { wght: 700 } });
    const box = (name: string) => q<HTMLInputElement>(el, `[aria-label="${name}"]`);
    box('Tabular figures').click();
    await fixture.whenStable();
    expect(saved.at(-1)).toMatchObject({
      stack: 'lora-stack',
      axes: { wght: 700 },
      features: { tnum: true },
    });
    box('Tabular figures').click();
    await fixture.whenStable();
    expect(saved.at(-1)?.features).toEqual({});
    box('Ligatures').click();
    await fixture.whenStable();
    expect(saved.at(-1)?.features).toEqual({ liga: false });
    box('Ligatures').click();
    await fixture.whenStable();
    expect(saved.at(-1)?.features).toEqual({ liga: true });
  });

  it('keeps the controls in the typography token editor', async () => {
    const { el } = await setup(INTER_STACK_ID, {}, true);
    expect(q(el, '[aria-label="Weight"]')).not.toBeNull();
    expect(q(el, '[aria-label="Italic"]')).not.toBeNull();
  });

  describe('approximation cue', () => {
    const cue = (el: HTMLElement) => q<HTMLElement>(el, '.cue');

    it('is hidden when nothing is approximated', async () => {
      const { el } = await setup('lora-stack');
      expect(cue(el)).toBeNull();
      core.textFallback.mockReturnValue({ approximated: false, missing: false, generic: 'Source Serif 4' });
      const next = await setup('lora-stack');
      expect(cue(next.el)).toBeNull();
    });

    it('names the generic face when approximated', async () => {
      core.textFallback.mockReturnValue({ approximated: true, missing: false, generic: 'Source Serif 4' });
      const { el } = await setup('lora-stack');
      expect(cue(el).getAttribute('aria-label')).toBe('Drawn with Source Serif 4 in place of serif');
    });

    it('reports missing glyphs, alone or with the approximation', async () => {
      core.textFallback.mockReturnValue({ approximated: false, missing: true, generic: null });
      const { el } = await setup('lora-stack');
      expect(cue(el).getAttribute('aria-label')).toBe('Some characters have no glyph');
      core.textFallback.mockReturnValue({ approximated: true, missing: true, generic: 'Source Serif 4' });
      const both = await setup('lora-stack');
      expect(cue(both.el).getAttribute('aria-label')).toBe(
        'Drawn with Source Serif 4 in place of serif\nSome characters have no glyph',
      );
    });

    it('is not shown in the typography token editor', async () => {
      core.textFallback.mockReturnValue({ approximated: true, missing: true, generic: 'Source Serif 4' });
      const { el } = await setup('lora-stack', {}, true);
      expect(cue(el)).toBeNull();
    });

    it('updates on availability changes and when a family finishes loading', async () => {
      const { fixture, service, el } = await setup('lora-stack');
      expect(cue(el)).toBeNull();
      core.textFallback.mockReturnValue({ approximated: true, missing: false, generic: 'Source Serif 4' });
      service.setAvailable('lora', false);
      await fixture.whenStable();
      expect(cue(el)).not.toBeNull();
      core.textFallback.mockReturnValue(null);
      service.loadedFamilies.update((n) => n + 1);
      await fixture.whenStable();
      expect(cue(el)).toBeNull();
    });
  });
});
