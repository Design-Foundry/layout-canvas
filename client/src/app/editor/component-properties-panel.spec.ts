import { TestBed } from '@angular/core/testing';
import type { ComponentDefinition, ComponentValues, NodeProps } from '@canvas/bridge';
import { ComponentPropertiesPanel } from './component-properties-panel';

describe('declared component properties', () => {
  async function ruleSetup() {
    const setupResult = await setup('component');
    const original: ComponentDefinition = {
      ...definition,
      rules: [
        {
          target: 'icon',
          when: { size: 'large' },
          patch: { opacity: 0, textStyle: { fontSize: 32, lineHeight: 2, align: 'center' } },
        },
      ],
    };
    setupResult.fixture.componentRef.setInput('node', {
      id: 'main',
      type: 'component',
      definition: original,
    } as unknown as NodeProps);
    await setupResult.fixture.whenStable();
    return { ...setupResult, original };
  }
  it('edits a single existing metric on blur and alignment without replacing sibling fields', async () => {
    const { fixture, saves, element } = await ruleSetup();
    const input = element.querySelector<HTMLInputElement>(
      '[aria-label="Font size in variant style 1"]',
    )!;
    input.value = '40';
    input.dispatchEvent(new Event('input'));
    expect(saves).toHaveLength(0);
    input.dispatchEvent(new Event('blur'));
    await fixture.whenStable();
    const alignment = element.querySelector<HTMLSelectElement>(
      '[aria-label="Text alignment in variant style 1"]',
    )!;
    alignment.value = 'right';
    alignment.dispatchEvent(new Event('change'));
    await fixture.whenStable();
    expect(saves.at(-1)?.rules[0].patch).toEqual({
      opacity: 0,
      textStyle: { fontSize: 40, lineHeight: 2, align: 'right' },
    });
    expect(saves.at(-1)?.rules[0].when).toEqual({ size: 'large' });
  });
  it('removes one style field, then its empty group and finally its empty rule', async () => {
    const { fixture, saves, original, element } = await ruleSetup();
    element
      .querySelector<HTMLButtonElement>('[aria-label="Inherit Font size in variant style 1"]')!
      .click();
    const panel = fixture.componentInstance as any;
    expect(saves.at(-1)?.rules[0].patch).toEqual({
      opacity: 0,
      textStyle: { lineHeight: 2, align: 'center' },
    });
    panel.removeRuleField(0, 'textStyle.lineHeight');
    panel.removeRuleField(0, 'textStyle.align');
    expect(saves.at(-1)?.rules[0].patch).toEqual({ opacity: 0 });
    panel.removeRuleField(0, 'opacity');
    expect(saves.at(-1)?.rules).toEqual([]);
    expect(original.rules[0].patch.textStyle?.fontSize).toBe(32);
  });
  it('restores invalid or native-rejected metrics and retains rejected field removals', async () => {
    const { fixture, saves, element } = await ruleSetup();
    const input = element.querySelector<HTMLInputElement>(
      '[aria-label="Font size in variant style 1"]',
    )!;
    for (const value of ['', '0', '513', 'NaN']) {
      input.value = value;
      input.dispatchEvent(new Event('input'));
      input.dispatchEvent(new Event('blur'));
      await fixture.whenStable();
      expect(input.value).toBe('32');
    }
    fixture.componentRef.setInput('save', () => false);
    await fixture.whenStable();
    input.value = '40';
    input.dispatchEvent(new Event('input'));
    input.dispatchEvent(new Event('blur'));
    await fixture.whenStable();
    expect(input.value).toBe('32');
    element
      .querySelector<HTMLButtonElement>('[aria-label="Inherit Font size in variant style 1"]')!
      .click();
    await fixture.whenStable();
    expect(element.querySelector('[aria-label="Font size in variant style 1"]')).not.toBeNull();
    expect(saves).toHaveLength(0);
  });
  it('preserves token references and removes the fill visibility companion with its paint', async () => {
    const { fixture, saves } = await setup('component');
    fixture.componentRef.setInput('node', {
      id: 'main',
      type: 'component',
      definition: {
        ...definition,
        rules: [
          {
            target: 'icon',
            when: { size: 'large' },
            patch: {
              fill: { token: 'accent' },
              fillVisible: true,
              w: { token: 'space' },
              opacity: 0,
            },
          },
        ],
      },
    } as unknown as NodeProps);
    await fixture.whenStable();
    const panel = fixture.componentInstance as any;
    const fields = panel.ruleFields(panel.definition().rules[0].patch);
    expect(fields.find((f: any) => f.key === 'w').editable).toBe(false);
    expect(fields.some((f: any) => f.key === 'fillVisible')).toBe(false);
    panel.removeRuleField(0, 'fill');
    expect(saves.at(-1)?.rules[0].patch).toEqual({ w: { token: 'space' }, opacity: 0 });
    panel.removeRuleField(0, 'not-authored');
    expect(saves).toHaveLength(1);
  });
  it('adds and reorders existing axis choices without resetting defaults, rules or other axes', async () => {
    const { fixture, saves, element } = await setup('component');
    const panel = fixture.componentInstance as any;
    panel.ruleTarget = 'icon';
    panel.cell = { size: 'large' };
    panel.ruleField = 'w';
    panel.ruleValue = '32';
    panel.addRule();
    await fixture.whenStable();
    const choices = element.querySelector<HTMLInputElement>('[aria-label="Choices for size"]')!;
    choices.value = 'large, compact, roomy';
    choices.dispatchEvent(new Event('input'));
    expect(saves).toHaveLength(1);
    choices.dispatchEvent(new Event('blur'));
    await fixture.whenStable();
    expect(saves.at(-1)?.axes[0]).toEqual({
      name: 'size',
      values: ['large', 'compact', 'roomy'],
      default: 'compact',
    });
    expect(saves.at(-1)?.axes[1]).toEqual(definition.axes[1]);
    expect(saves.at(-1)?.rules[0].when).toEqual({ size: 'large' });
  });
  it('restores invalid choices and prevents removing a default or a style condition', async () => {
    const { fixture, saves, element } = await setup('component');
    const choices = element.querySelector<HTMLInputElement>('[aria-label="Choices for size"]')!;
    const edit = async (value: string) => {
      choices.value = value;
      choices.dispatchEvent(new Event('input'));
      choices.dispatchEvent(new Event('blur'));
      await fixture.whenStable();
      expect(choices.value).toBe('compact, large');
    };
    for (const invalid of [
      '',
      'compact, compact',
      'Compact, large',
      Array.from({ length: 33 }, (_, i) => `v${i}`).join(','),
    ])
      await edit(invalid);
    await edit('large');
    expect(element.querySelector('[role=alert]')?.textContent).toContain('another default');
    const panel = fixture.componentInstance as any;
    panel.ruleTarget = 'icon';
    panel.cell = { size: 'large' };
    panel.ruleField = 'w';
    panel.ruleValue = '32';
    panel.addRule();
    await fixture.whenStable();
    await edit('compact');
    expect(element.querySelector('[role=alert]')?.textContent).toContain('variant styles');
    expect(saves).toHaveLength(1);
  });
  it('restores native-rejected axis edits and defaults without discarding earlier accepted choices', async () => {
    const { fixture, saves, element } = await setup('component');
    const choices = element.querySelector<HTMLInputElement>('[aria-label="Choices for size"]')!;
    choices.value = 'compact, large, roomy';
    choices.dispatchEvent(new Event('input'));
    choices.dispatchEvent(new Event('blur'));
    await fixture.whenStable();
    fixture.componentRef.setInput('save', () => false);
    await fixture.whenStable();
    choices.value = 'compact, roomy';
    choices.dispatchEvent(new Event('input'));
    choices.dispatchEvent(new Event('blur'));
    await fixture.whenStable();
    expect(choices.value).toBe('compact, large, roomy');
    const selector = element.querySelector<HTMLSelectElement>('[aria-label="size default"]')!;
    selector.value = 'large';
    selector.dispatchEvent(new Event('change'));
    await fixture.whenStable();
    expect(selector.value).toBe('compact');
    expect(saves).toHaveLength(1);
    expect(element.querySelector('[role=alert]')?.textContent).toContain('used by an instance');
  });
  it('removes an unused choice and clears only its unsaved condition', async () => {
    const { fixture, saves, element } = await setup('component');
    const panel = fixture.componentInstance as any;
    panel.cell = { size: 'large', tone: 'danger' };
    const choices = element.querySelector<HTMLInputElement>('[aria-label="Choices for size"]')!;
    choices.value = 'compact';
    choices.dispatchEvent(new Event('input'));
    choices.dispatchEvent(new Event('blur'));
    await fixture.whenStable();
    expect(saves.at(-1)?.axes[0].values).toEqual(['compact']);
    expect(panel.cell).toEqual({ tone: 'danger' });
  });
  it('merges individual typography fields in a variant without replacing its prior fields', async () => {
    const { fixture, saves } = await setup('component');
    fixture.componentRef.setInput('snapshot', () => ({
      nodes: [
        { id: 'main', type: 'component' },
        { id: 'text', parent: 'main', type: 'text', name: 'Label' },
      ],
    }));
    await fixture.whenStable();
    const panel = fixture.componentInstance as any;
    panel.ruleTarget = 'text';
    panel.cell = { size: 'large' };
    panel.ruleField = 'fontSize';
    panel.ruleValue = '32';
    panel.addRule();
    panel.ruleField = 'lineHeight';
    panel.ruleValue = '2';
    panel.addRule();
    panel.ruleField = 'align';
    panel.ruleValue = 'center';
    panel.addRule();
    expect(saves.at(-1)?.rules[0].patch).toEqual({
      textStyle: { fontSize: 32, lineHeight: 2, align: 'center' },
    });
    expect(panel.patchLabel(saves.at(-1)?.rules[0].patch)).toContain('fontSize: 32');
  });
  it('refuses text fields on non-text anatomy and invalid metrics before saving', async () => {
    const { fixture, saves } = await setup('component');
    fixture.componentRef.setInput('snapshot', () => ({
      nodes: [
        { id: 'main', type: 'component' },
        { id: 'text', parent: 'main', type: 'text' },
      ],
    }));
    await fixture.whenStable();
    const panel = fixture.componentInstance as any;
    panel.cell = { size: 'large' };
    panel.ruleTarget = 'main';
    panel.ruleField = 'fontSize';
    panel.ruleValue = '32';
    panel.addRule();
    expect(saves).toHaveLength(0);
    panel.ruleTarget = 'text';
    for (const invalid of ['', '0', '513', 'NaN']) {
      panel.ruleValue = invalid;
      panel.addRule();
    }
    expect(saves).toHaveLength(0);
  });
  it('edits and resets a text choice without clearing an independent axis', async () => {
    const { fixture, values, element } = await setup();
    fixture.componentRef.setInput('node', {
      id: 'main',
      type: 'instance',
      definition: {
        ...definition,
        texts: [{ name: 'label', target: 'text', default: 'Continuar' }],
      },
      componentValues: { tone: 'danger' },
    } as unknown as NodeProps);
    await fixture.whenStable();
    const textarea = element.querySelector('textarea')!;
    expect(textarea.value).toBe('Continuar');
    textarea.value = 'Guardar diseño';
    textarea.dispatchEvent(new Event('input'));
    textarea.dispatchEvent(new Event('blur'));
    await fixture.whenStable();
    expect(values.at(-1)).toEqual({ tone: 'danger', label: 'Guardar diseño' });
    element.querySelector<HTMLButtonElement>('[aria-label="Use default for label"]')!.click();
    expect(values.at(-1)).toEqual({ tone: 'danger' });
  });
  it('authors a named text property bound to actual text anatomy', async () => {
    const { fixture, saves } = await setup('component');
    fixture.componentRef.setInput('snapshot', () => ({
      nodes: [
        { id: 'main', type: 'component' },
        { id: 'text', type: 'text', parent: 'main', name: 'Label' },
      ],
    }));
    await fixture.whenStable();
    const panel = fixture.componentInstance as any;
    panel.textName = 'label';
    panel.textTarget = 'text';
    panel.textDefault = 'Jose\u0301';
    panel.addText();
    expect(saves.at(-1)?.texts).toEqual([{ name: 'label', target: 'text', default: 'José' }]);
  });
  const definition: ComponentDefinition = {
    properties: [{ name: 'show-icon', target: 'icon', default: true }],
    axes: [
      { name: 'size', values: ['compact', 'large'], default: 'compact' },
      { name: 'tone', values: ['neutral', 'danger'], default: 'neutral' },
    ],
    rules: [],
  };
  async function setup(type: 'instance' | 'component' = 'instance', inInstance = false) {
    await TestBed.configureTestingModule({
      imports: [ComponentPropertiesPanel],
    }).compileComponents();
    const fixture = TestBed.createComponent(ComponentPropertiesPanel);
    const saves: ComponentDefinition[] = [];
    const values: ComponentValues[] = [];
    fixture.componentRef.setInput('node', {
      id: 'main',
      type,
      inInstance,
      definition,
      componentValues: { tone: 'danger' },
    } as unknown as NodeProps);
    fixture.componentRef.setInput('save', (_: string, d: ComponentDefinition) => {
      saves.push(d);
      return true;
    });
    fixture.componentRef.setInput('snapshot', () => ({
      nodes: [
        { id: 'main', type: 'component', name: 'Button' },
        { id: 'icon', parent: 'main', type: 'rect', name: 'Icon' },
      ],
    }));
    fixture.componentRef.setInput('colorFromHex', () => null);
    fixture.componentRef.setInput('saveValues', (_: string, v: ComponentValues) => {
      values.push(v);
      return true;
    });
    await fixture.whenStable();
    return { fixture, saves, values, element: fixture.nativeElement as HTMLElement };
  }
  it('shows inherited defaults, changes one axis without clearing other choices, and resets independently', async () => {
    const { fixture, values, element } = await setup();
    const size = element.querySelector('select')!;
    expect(size.value).toBe('compact');
    size.value = 'large';
    size.dispatchEvent(new Event('change'));
    await fixture.whenStable();
    expect(values).toEqual([{ tone: 'danger', size: 'large' }]);
    element.querySelector<HTMLButtonElement>('button[aria-label="Use default for tone"]')!.click();
    expect(values.at(-1)).toEqual({ size: 'large' });
    expect(definition.axes[0].default).toBe('compact');
  });
  it('merges rapid edits before the next native frame instead of replacing an earlier choice', async () => {
    const { fixture, values } = await setup();
    const panel = fixture.componentInstance as any;
    panel.choose('size', 'large');
    panel.choose('show-icon', false);
    panel.choose('tone', 'neutral');
    expect(values.at(-1)).toEqual({ tone: 'neutral', size: 'large', 'show-icon': false });
    fixture.componentRef.setInput('node', {
      id: 'main',
      type: 'instance',
      definition,
      componentValues: {},
    } as unknown as NodeProps);
    await fixture.whenStable();
    expect(panel.value('size', 'compact')).toBe('compact');
  });
  it('retains consecutive definition edits while the next native frame is pending', async () => {
    const { fixture, saves } = await setup('component');
    const panel = fixture.componentInstance as any;
    panel.axisDefaultChanged('size', 'large', { control: { setValue: () => {} } });
    panel.axisDefaultChanged('tone', 'danger', { control: { setValue: () => {} } });
    expect(saves.at(-1)?.axes.map((a) => a.default)).toEqual(['large', 'danger']);
    expect(definition.axes.map((a) => a.default)).toEqual(['compact', 'neutral']);
  });
  it('authors a visibility binding to a named layer and rejects malformed names before saving', async () => {
    const { fixture, saves, element } = await setup('component');
    const panel = fixture.componentInstance as any;
    panel.propertyName = 'Show Icon';
    panel.propertyTarget = 'icon';
    panel.addProperty();
    await fixture.whenStable();
    expect(saves).toEqual([]);
    expect(element.querySelector('[role=alert]')).not.toBeNull();
    panel.propertyName = 'show-label';
    panel.propertyDefault = false;
    panel.addProperty();
    expect(saves[0].properties.at(-1)).toEqual({
      name: 'show-label',
      target: 'icon',
      default: false,
    });
    expect(definition.properties.length).toBe(1);
  });
  it('authors partial cells and surfaces native rejection without removing existing definitions', async () => {
    const { fixture, saves, element } = await setup('component');
    const panel = fixture.componentInstance as any;
    panel.cell = { size: 'large', tone: '' };
    panel.ruleTarget = 'icon';
    panel.ruleField = 'w';
    panel.ruleValue = '32';
    panel.addRule();
    expect(saves[0].rules).toEqual([{ when: { size: 'large' }, target: 'icon', patch: { w: 32 } }]);
    fixture.componentRef.setInput('save', () => false);
    panel.removeAxis('tone');
    await fixture.whenStable();
    expect(element.querySelector('[role=alert]')?.textContent).toContain('used by an instance');
    expect(definition.axes.length).toBe(2);
  });
  it('hides controls for virtual copies whose declared selections are not yet editable', async () => {
    const { element } = await setup('instance', true);
    expect(element.querySelector('section')).toBeNull();
  });
  const swapDefinition: ComponentDefinition = {
    properties: [],
    axes: [],
    rules: [],
    swaps: [{ name: 'icon', target: 'slot', default: 'a', choices: ['a', 'b'] }],
  };
  const swapSnapshot = () => ({
    nodes: [
      { id: 'main', type: 'component', name: 'Button' },
      { id: 'slot', type: 'instance', parent: 'main', component: 'a', name: 'Icon slot' },
      { id: 'a', type: 'component', name: 'Icon A' },
      { id: 'b', type: 'component', name: 'Icon B' },
    ],
  });
  it('selects a named component and restores the previous visible choice if native validation refuses', async () => {
    const { fixture, values, element } = await setup();
    fixture.componentRef.setInput('snapshot', swapSnapshot);
    fixture.componentRef.setInput('node', {
      id: 'instance',
      type: 'instance',
      definition: swapDefinition,
      componentValues: {},
    } as unknown as NodeProps);
    await fixture.whenStable();
    const control = element.querySelector<HTMLSelectElement>('select[aria-label="icon"]')!;
    expect(control.options[0].textContent?.trim()).toBe('Icon A');
    control.value = 'b';
    control.dispatchEvent(new Event('change'));
    await fixture.whenStable();
    expect(values.at(-1)).toEqual({ icon: 'b' });
    fixture.componentRef.setInput('saveValues', () => false);
    await fixture.whenStable();
    control.value = 'a';
    control.dispatchEvent(new Event('change'));
    await fixture.whenStable();
    expect(control.value).toBe('b');
    expect(element.querySelector('[role=alert]')?.textContent).toContain('Reset overrides');
  });
  it('authors a bounded choice list using component names and excludes the enclosing main', async () => {
    const { fixture, saves } = await setup('component');
    fixture.componentRef.setInput('snapshot', swapSnapshot);
    await fixture.whenStable();
    const panel = fixture.componentInstance as any;
    panel.setSwapTarget('slot');
    expect(panel.swapChoices().map((c: any) => c.value)).toEqual(['a', 'b']);
    panel.swapName = 'icon';
    panel.allowSwap('a', true);
    panel.allowSwap('b', true);
    panel.swapDefault = 'a';
    panel.addSwap();
    expect(saves.at(-1)?.swaps).toEqual(swapDefinition.swaps);
  });
  it('restores a rejected swap default and hides owned layer selection controls', async () => {
    const { fixture, element } = await setup('component');
    fixture.componentRef.setInput('snapshot', swapSnapshot);
    fixture.componentRef.setInput('node', {
      id: 'main',
      type: 'component',
      definition: swapDefinition,
    } as unknown as NodeProps);
    fixture.componentRef.setInput('save', () => false);
    await fixture.whenStable();
    const control = element.querySelector<HTMLSelectElement>('select[aria-label="icon default"]')!;
    control.value = 'b';
    control.dispatchEvent(new Event('change'));
    await fixture.whenStable();
    expect(control.value).toBe('a');
    expect(element.querySelector('[role=alert]')).not.toBeNull();
    fixture.componentRef.setInput('node', {
      id: 'slot',
      type: 'instance',
      swapBound: true,
      definition: swapDefinition,
    } as unknown as NodeProps);
    await fixture.whenStable();
    expect(element.querySelector('section')).toBeNull();
  });
});
