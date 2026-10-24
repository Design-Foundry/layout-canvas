import { TestBed } from '@angular/core/testing';
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
        font: 'inter-regular-v1',
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
});
