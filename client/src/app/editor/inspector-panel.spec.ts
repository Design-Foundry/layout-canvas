import { TestBed } from '@angular/core/testing';
import { INTER_STACK_ID } from '@canvas/bridge';
import type { NodeProps, NodePatch } from '@canvas/bridge';
import { InspectorPanel } from './inspector-panel';

describe('component inspector', () => {
  const node = (type: NodeProps['type']): NodeProps => ({
    id: 'instance',
    type,
    name: 'Button',
    x: 0,
    y: 0,
    w: 120,
    h: 40,
    box: { x: 0, y: 0, w: 120, h: 40 },
    fill: { l: 1, c: 0, h: 0, a: 1, hex: '#ffffff' },
    fillVisible: true,
    cornerRadius: 0,
    cornerRadii: [null, null, null, null],
    stroke: { width: 0, style: 'solid', align: 'inside', color: { l: 0, c: 0, h: 0, a: 1 } },
    shadows: [],
    opacity: 1,
    visible: true,
    locked: false,
    clipContent: false,
    layout: { type: 'row', gap: 0, padding: [0, 0, 0, 0], align: 'start', justify: 'start' },
    widthSizing: 'fixed',
    heightSizing: 'fixed',
    limits: {},
    absolute: false,
    inFlow: false,
    parentLayout: 'none',
    component: 'main',
    overrides: 0,
  });
  async function setup(value: NodeProps) {
    await TestBed.configureTestingModule({ imports: [InspectorPanel] }).compileComponents();
    const fixture = TestBed.createComponent(InspectorPanel);
    fixture.componentRef.setInput('node', value);
    fixture.componentRef.setInput('components', [
      { label: 'Main', value: 'main' },
      { label: 'Other', value: 'other' },
    ]);
    fixture.componentRef.setInput('colorFromHex', () => null);
    fixture.componentRef.setInput('colorToHex', () => '#ffffff');
    await fixture.whenStable();
    return fixture;
  }
  it('exposes native text, Hug and fill without unsupported box decorations', async () => {
    const fixture = await setup({
      ...node('text'),
      layout: { ...node('text').layout, type: 'none' },
      text: {
        content: 'Texto',
        stack: INTER_STACK_ID,
        fontSize: 24,
        lineHeight: 1.4,
        letterSpacing: 0,
        align: 'left',
      },
    });
    const element = fixture.nativeElement as HTMLElement;
    expect(element.querySelector('textarea')).not.toBeNull();
    expect(element.textContent).not.toContain('Auto layout');
    expect(element.querySelector('[aria-label="Add stroke"]')).toBeNull();
    expect(element.querySelector('[aria-label="Add shadow"]')).toBeNull();
    expect(
      [...element.querySelectorAll('button')]
        .filter((b) => b.textContent?.trim() === 'Hug')
        .every((b) => !b.disabled),
    ).toBe(true);
  });
  it('explains a property-bound instance and blocks its direct main swap and detach', async () => {
    const fixture = await setup({ ...node('instance'), swapBound: true });
    const element = fixture.nativeElement as HTMLElement;
    expect((fixture.componentInstance as any).componentControl.disabled).toBe(true);
    expect(element.querySelector('[aria-label="Detach instance"]')).toBeNull();
    expect(element.textContent).toContain('controlled by an instance swap property');
    const edits: NodePatch[] = [];
    fixture.componentInstance.edit.subscribe((p) => edits.push(p));
    (fixture.componentInstance as any).componentControl.setValue('other');
    expect(edits).toEqual([]);
  });
  it('makes copied children owned by a swap-bound template instance read-only', async () => {
    const fixture = await setup({ ...node('rect'), inInstance: true, swapBound: true });
    const element = fixture.nativeElement as HTMLElement;
    expect(element.querySelector('fieldset:disabled')).not.toBeNull();
    expect(element.textContent).toContain('Go to main to edit the source');
    expect(element.querySelector('input:disabled')).not.toBeNull();
  });
  it.each(['component', 'instance'] as const)(
    'exposes auto layout, Hug and clipping for %s',
    async (type) => {
      const fixture = await setup(node(type));
      const element = fixture.nativeElement as HTMLElement;
      expect(element.textContent).toContain('Auto layout');
      expect(element.textContent).toContain('Clip content');
      const hug = [...element.querySelectorAll('button')].filter(
        (b) => b.textContent?.trim() === 'Hug',
      );
      expect(hug.length).toBe(2);
      expect(hug.every((b) => !b.disabled)).toBe(true);
    },
  );
  it('does not offer container-only layout controls for a rectangle', async () => {
    const fixture = await setup(node('rect'));
    expect(fixture.nativeElement.textContent).not.toContain('Auto layout');
    expect(fixture.nativeElement.textContent).not.toContain('Clip content');
  });
  it('blocks swapping overridden instances until an explicit reset', async () => {
    const fixture = await setup({ ...node('instance'), overrides: 1 });
    const edits: NodePatch[] = [];
    fixture.componentInstance.edit.subscribe((p) => edits.push(p));
    const control = (fixture.componentInstance as any).componentControl;
    expect(control.disabled).toBe(true);
    control.setValue('other');
    expect(edits).toEqual([]);
    fixture.componentRef.setInput('node', node('instance'));
    await fixture.whenStable();
    expect(control.disabled).toBe(false);
    control.setValue('other');
    expect(edits).toEqual([{ component: 'other' }]);
  });
  it('offers a targeted layer reset for an overridden child without offering a main swap', async () => {
    const fixture = await setup({ ...node('rect'), inInstance: true, overrides: 1 });
    const commands: string[] = [];
    fixture.componentInstance.componentCommand.subscribe((c) => commands.push(c));
    const element = fixture.nativeElement as HTMLElement;
    expect(element.textContent).not.toContain('Main component');
    const reset = [...element.querySelectorAll('button')].find(
      (b) => b.textContent?.trim() === 'Reset layer overrides',
    )!;
    expect(reset).toBeDefined();
    reset.click();
    expect(commands).toEqual(['resetOverrides']);
  });
  it('does not offer detach or main-swap actions on a copied nested instance', async () => {
    const fixture = await setup({ ...node('instance'), inInstance: true });
    const element = fixture.nativeElement as HTMLElement;
    expect(element.textContent).not.toContain('Main component');
    expect(element.querySelector('button[aria-label="Detach instance"]')).toBeNull();
  });
  describe('notes', () => {
    const field = (fixture: { nativeElement: HTMLElement }) =>
      fixture.nativeElement.querySelector('textarea[aria-label="Notes"]') as HTMLTextAreaElement | null;
    const type = (input: HTMLTextAreaElement, value: string) => {
      input.value = value;
      input.dispatchEvent(new Event('change'));
    };
    it('shows the notes of the selected layer', async () => {
      const fixture = await setup({ ...node('rect'), notes: 'Ask legal' });
      expect(field(fixture)!.value).toBe('Ask legal');
    });
    it('is empty for a layer without notes', async () => {
      expect(field(await setup(node('rect')))!.value).toBe('');
    });
    it('is not offered for a copy inside an instance', async () => {
      expect(field(await setup({ ...node('rect'), inInstance: true }))).toBeNull();
    });
    it('saves multiline text and clears it with an empty value', async () => {
      const fixture = await setup({ ...node('rect'), notes: 'old' });
      const saved: [string, NodePatch][] = [];
      fixture.componentRef.setInput('saveStyleBinding', (id: string, patch: NodePatch) => {
        saved.push([id, patch]);
        return true;
      });
      const input = field(fixture)!;
      type(input, 'one\ntwo');
      type(input, '');
      expect(saved).toEqual([
        ['instance', { notes: 'one\ntwo' }],
        ['instance', { notes: '' }],
      ]);
    });
    it('restores the saved text when the change is refused', async () => {
      const fixture = await setup({ ...node('rect'), notes: 'kept' });
      fixture.componentRef.setInput('saveStyleBinding', () => false);
      const input = field(fixture)!;
      type(input, 'refused');
      expect(input.value).toBe('kept');
    });
  });
  describe('part name', () => {
    const field = (fixture: { nativeElement: HTMLElement }) =>
      fixture.nativeElement.querySelector('input[aria-label="Part"]') as HTMLInputElement | null;
    const type = (input: HTMLInputElement, value: string) => {
      input.value = value;
      input.dispatchEvent(new Event('change'));
    };
    it('is offered for a stored layer inside a main component', async () => {
      expect(field(await setup({ ...node('rect'), inComponent: true }))).not.toBeNull();
    });
    it('is not offered outside a component', async () => {
      expect(field(await setup(node('rect')))).toBeNull();
    });
    it('is not offered for a copy inside an instance', async () => {
      expect(
        field(await setup({ ...node('rect'), inComponent: true, inInstance: true })),
      ).toBeNull();
    });
    it('saves a kebab-case name and clears it with an empty value', async () => {
      const fixture = await setup({ ...node('rect'), inComponent: true, part: 'label' });
      const saved: [string, NodePatch][] = [];
      fixture.componentRef.setInput('saveStyleBinding', (id: string, patch: NodePatch) => {
        saved.push([id, patch]);
        return true;
      });
      const input = field(fixture)!;
      expect(input.value).toBe('label');
      type(input, 'icon-2');
      type(input, '');
      expect(saved).toEqual([
        ['instance', { part: 'icon-2' }],
        ['instance', { part: '' }],
      ]);
    });
    it('restores the saved value and says so when the name is invalid or refused', async () => {
      const fixture = await setup({ ...node('rect'), inComponent: true, part: 'label' });
      const saved: NodePatch[] = [];
      fixture.componentRef.setInput('saveStyleBinding', (_: string, patch: NodePatch) => {
        saved.push(patch);
        return false;
      });
      const input = field(fixture)!;
      type(input, 'Not Kebab');
      await fixture.whenStable();
      expect(saved).toEqual([]);
      expect(input.value).toBe('label');
      expect(fixture.nativeElement.querySelector('[role="alert"]')).not.toBeNull();
      type(input, 'taken');
      await fixture.whenStable();
      expect(saved).toEqual([{ part: 'taken' }]);
      expect(input.value).toBe('label');
      expect(fixture.nativeElement.querySelector('[role="alert"]')).not.toBeNull();
    });
  });
  it('shows image fit and crop with opacity but hides unsupported paint and sizing controls', async () => {
    const fixture = await setup({
      ...node('image'),
      image: { width: 1, height: 1, id: 'a'.repeat(64), pixels: 'AAAA/w==' },
      imageFit: 'cover',
      imageCrop: { x: 0, y: 0, w: 1, h: 1 },
    });
    const element = fixture.nativeElement as HTMLElement;
    expect(element.querySelector('app-image-panel')).not.toBeNull();
    expect(element.textContent).toContain('Opacity');
    expect(element.querySelector('app-color-field')).toBeNull();
    expect(element.querySelector('[aria-label="Add stroke"]')).toBeNull();
    expect(element.querySelector('[aria-label="Add shadow"]')).toBeNull();
    expect(
      [...element.querySelectorAll('button')].some((b) => b.textContent?.trim() === 'Hug'),
    ).toBe(false);
  });
  it('shows the icon section with fill, opacity and sizing but no stroke, effects or radius', async () => {
    const fixture = await setup({
      ...node('icon'),
      layout: { ...node('icon').layout, type: 'none' },
      icon: { set: '00000000-0000-0000-0000-000000000200', glyph: 'arrow_back' },
    });
    const element = fixture.nativeElement as HTMLElement;
    expect(element.querySelector('app-icon-panel')).not.toBeNull();
    expect((element.querySelector('[aria-label="Glyph"]') as HTMLInputElement).value).toBe('arrow_back');
    expect(element.querySelector('app-color-field')).not.toBeNull();
    expect(element.textContent?.toLowerCase()).toContain('opacity');
    expect(element.textContent).not.toContain('Corner radius');
    expect(element.textContent).not.toContain('Auto layout');
    expect(element.querySelector('[aria-label="Add stroke"]')).toBeNull();
    expect(element.querySelector('[aria-label="Add shadow"]')).toBeNull();
    expect(
      [...element.querySelectorAll('button')].some((b) => b.textContent?.trim() === 'Hug'),
    ).toBe(true);
  });
});
