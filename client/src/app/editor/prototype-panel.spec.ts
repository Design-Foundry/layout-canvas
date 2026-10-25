import { TestBed } from '@angular/core/testing';
import { PrototypePanel } from './prototype-panel';
import type { NodeProps, NodePatch, PrototypeScreen } from '@canvas/bridge';
const screens: PrototypeScreen[] = [
  { id: 'frame-a', name: 'Home', start: true, w: 300, h: 500 },
  { id: 'frame-b', name: 'Details', start: false, w: 300, h: 500 },
];
async function setup(
  accept = true,
  node = { id: 'button', type: 'rect' } as NodeProps,
  initialScreens: PrototypeScreen[] = screens,
) {
  await TestBed.configureTestingModule({ imports: [PrototypePanel] }).compileComponents();
  const fixture = TestBed.createComponent(PrototypePanel),
    saved: { id: string; patch: NodePatch }[] = [];
  fixture.componentRef.setInput('node', node);
  fixture.componentRef.setInput('screens', initialScreens);
  fixture.componentRef.setInput('save', (id: string, patch: NodePatch) => {
    saved.push({ id, patch });
    return accept;
  });
  await fixture.whenStable();
  const select = async (label: string, value: string) => {
    const control = fixture.nativeElement.querySelector(
      `select[aria-label="${label}"]`,
    ) as HTMLSelectElement;
    control.value = value;
    control.dispatchEvent(new Event('change'));
    await fixture.whenStable();
    return control;
  };
  return { fixture, saved, select };
}
describe('document-local prototype authoring', () => {
  it('shows a persisted second-screen target on initial render without writing an edit', async () => {
    const { fixture, saved } = await setup(true, {
      id: 'button',
      type: 'rect',
      interaction: { trigger: 'click', action: 'open-overlay', target: 'frame-b' },
    } as NodeProps);
    const destination = fixture.nativeElement.querySelector(
      '[aria-label="Prototype destination"]',
    ) as HTMLSelectElement;
    expect(destination.value).toBe('frame-b');
    expect(destination.selectedOptions[0].textContent?.trim()).toBe('Details');
    expect(saved).toEqual([]);
  });
  it('selects the persisted target when screen options arrive after the interaction', async () => {
    const { fixture, saved } = await setup(
      true,
      {
        id: 'button',
        type: 'rect',
        interaction: { trigger: 'click', action: 'navigate', target: 'frame-b' },
      } as NodeProps,
      [],
    );
    const destination = fixture.nativeElement.querySelector(
      '[aria-label="Prototype destination"]',
    ) as HTMLSelectElement;
    expect(destination.options.length).toBe(0);
    fixture.componentRef.setInput('screens', screens);
    await fixture.whenStable();
    expect(destination.value).toBe('frame-b');
    expect(destination.selectedOptions[0].textContent?.trim()).toBe('Details');
    expect(saved).toEqual([]);
  });
  it('writes navigate, named destination, back and explicit no-interaction edits', async () => {
    const { saved, select } = await setup();
    await select('Prototype action', 'navigate');
    expect(saved.at(-1)?.patch).toEqual({
      interaction: { trigger: 'click', action: 'navigate', target: 'frame-a' },
    });
    await select('Prototype destination', 'frame-b');
    expect(saved.at(-1)?.patch.interaction).toEqual({
      trigger: 'click',
      action: 'navigate',
      target: 'frame-b',
    });
    await select('Prototype action', 'back');
    expect(saved.at(-1)?.patch).toEqual({ interaction: { trigger: 'click', action: 'back' } });
    await select('Prototype action', 'none');
    expect(saved.at(-1)?.patch).toEqual({ interaction: null });
  });
  it('keeps a refused action out of pending metadata and shows a visible error', async () => {
    const { fixture, select } = await setup(false);
    const control = await select('Prototype action', 'navigate');
    expect(control.value).toBe('none');
    expect(fixture.nativeElement.querySelector('[role=alert]').textContent).toContain('refused');
    expect(fixture.nativeElement.querySelector('[aria-label="Prototype destination"]')).toBeNull();
  });
  it('shows the start flag only on authored root frames and restores refused state', async () => {
    const { fixture, saved } = await setup(false, {
      id: 'frame-a',
      type: 'frame',
      prototypeStart: false,
    } as NodeProps);
    const checkbox = fixture.nativeElement.querySelector('input') as HTMLInputElement;
    checkbox.click();
    await fixture.whenStable();
    expect(saved.at(-1)?.patch).toEqual({ prototypeStart: true });
    expect(checkbox.checked).toBe(false);
    fixture.componentRef.setInput('node', {
      id: 'nested',
      type: 'frame',
      parent: 'frame-a',
    } as NodeProps);
    await fixture.whenStable();
    expect(fixture.nativeElement.querySelector('input')).toBeNull();
  });
  it('saves copied-layer interactions through the native override callback', async () => {
    const { fixture, saved, select } = await setup(true, {
      id: 'copy',
      type: 'rect',
      inInstance: true,
    } as NodeProps);
    await select('Prototype action', 'back');
    expect(saved.at(-1)?.id).toBe('copy');
    expect(fixture.nativeElement.textContent).toContain('instance overrides');
    expect(fixture.nativeElement.querySelector('input')).toBeNull();
  });
  it('disables navigation when no root frame exists', async () => {
    const { fixture } = await setup();
    fixture.componentRef.setInput('screens', []);
    await fixture.whenStable();
    expect(
      (fixture.nativeElement.querySelector('option[value=navigate]') as HTMLOptionElement).disabled,
    ).toBe(true);
    expect(fixture.nativeElement.textContent).toContain('Frame tool (F)');
  });
  it('authors overlay dismissal and close actions without leaking target options', async () => {
    const { fixture, saved, select } = await setup();
    await select('Prototype action', 'open-overlay');
    expect(saved.at(-1)?.patch.interaction).toEqual({
      trigger: 'click',
      action: 'open-overlay',
      target: 'frame-a',
    });
    const outside = fixture.nativeElement.querySelector(
      '[aria-label="Dismiss overlay outside"]',
    ) as HTMLInputElement;
    expect(outside.checked).toBe(true);
    outside.click();
    await fixture.whenStable();
    expect(saved.at(-1)?.patch.interaction).toEqual({
      trigger: 'click',
      action: 'open-overlay',
      target: 'frame-a',
      dismissOutside: false,
    });
    await select('Prototype destination', 'frame-b');
    expect(saved.at(-1)?.patch.interaction).toEqual({
      trigger: 'click',
      action: 'open-overlay',
      target: 'frame-b',
      dismissOutside: false,
    });
    await select('Prototype action', 'close-overlay');
    expect(saved.at(-1)?.patch.interaction).toEqual({ trigger: 'click', action: 'close-overlay' });
    expect(fixture.nativeElement.querySelector('[aria-label="Prototype destination"]')).toBeNull();
    expect(fixture.nativeElement.querySelector('[aria-label="Prototype transition"]')).toBeNull();
  });
  it('keeps dissolve duration through destination changes and removes animation fields for instant', async () => {
    const { fixture, saved, select } = await setup();
    await select('Prototype action', 'navigate');
    await select('Prototype transition', 'dissolve');
    const duration = fixture.nativeElement.querySelector(
      '[aria-label="Prototype duration"]',
    ) as HTMLInputElement;
    expect(duration.value).toBe('300');
    duration.value = '625';
    duration.dispatchEvent(new Event('change'));
    await fixture.whenStable();
    await select('Prototype destination', 'frame-b');
    expect(saved.at(-1)?.patch.interaction).toEqual({
      trigger: 'click',
      action: 'navigate',
      target: 'frame-b',
      transition: 'dissolve',
      durationMs: 625,
    });
    await select('Prototype transition', 'instant');
    expect(saved.at(-1)?.patch.interaction).toEqual({
      trigger: 'click',
      action: 'navigate',
      target: 'frame-b',
    });
    expect(fixture.nativeElement.querySelector('[aria-label="Prototype duration"]')).toBeNull();
  });
  it('rejects blank, fractional and out-of-range duration before saving', async () => {
    const { fixture, saved, select } = await setup();
    await select('Prototype action', 'open-overlay');
    await select('Prototype transition', 'dissolve');
    const duration = fixture.nativeElement.querySelector(
        '[aria-label="Prototype duration"]',
      ) as HTMLInputElement,
      count = saved.length;
    for (const value of ['', '0', '2001', '1.5']) {
      duration.value = value;
      duration.dispatchEvent(new Event('change'));
      await fixture.whenStable();
      expect(duration.value).toBe('300');
    }
    expect(saved.length).toBe(count);
  });
  it('retains inherited animation and dismissal when a copied-layer edit is refused', async () => {
    const { fixture, saved, select } = await setup(false, {
      id: 'copy',
      type: 'rect',
      inInstance: true,
      interaction: {
        trigger: 'click',
        action: 'open-overlay',
        target: 'frame-b',
        transition: 'dissolve',
        durationMs: 700,
        dismissOutside: false,
      },
    } as NodeProps);
    const transition = await select('Prototype transition', 'instant');
    expect(transition.value).toBe('dissolve');
    const duration = fixture.nativeElement.querySelector(
      '[aria-label="Prototype duration"]',
    ) as HTMLInputElement;
    duration.value = '400';
    duration.dispatchEvent(new Event('change'));
    await fixture.whenStable();
    expect(duration.value).toBe('700');
    const outside = fixture.nativeElement.querySelector(
      '[aria-label="Dismiss overlay outside"]',
    ) as HTMLInputElement;
    outside.click();
    await fixture.whenStable();
    expect(outside.checked).toBe(false);
    expect(saved.every((entry) => entry.id === 'copy')).toBe(true);
    expect(fixture.nativeElement.querySelector('[role=alert]').textContent).toContain('refused');
  });
  it('clears push animation when switching to overlay and keeps push choices navigation-only', async () => {
    const { fixture, saved, select } = await setup();
    await select('Prototype action', 'navigate');
    await select('Prototype transition', 'push-left');
    expect(saved.at(-1)?.patch.interaction).toEqual({
      trigger: 'click',
      action: 'navigate',
      target: 'frame-a',
      transition: 'push-left',
      durationMs: 300,
    });
    await select('Prototype action', 'open-overlay');
    expect(saved.at(-1)?.patch.interaction).toEqual({
      trigger: 'click',
      action: 'open-overlay',
      target: 'frame-a',
    });
    expect(fixture.nativeElement.querySelector('[aria-label="Prototype duration"]')).toBeNull();
    expect(fixture.nativeElement.querySelector('option[value="push-left"]')).toBeNull();
    const count = saved.length;
    const rejected = await select('Prototype transition', 'push-right');
    expect(rejected.value).toBe('instant');
    expect(saved.length).toBe(count);
  });
  it('writes directional animations and retains duration through destination changes', async () => {
    const { fixture, saved, select } = await setup();
    await select('Prototype action', 'navigate');
    for (const transition of [
      'slide-left',
      'slide-right',
      'slide-up',
      'slide-down',
      'push-left',
      'push-right',
      'push-up',
      'push-down',
    ]) {
      await select('Prototype transition', transition);
      const duration = fixture.nativeElement.querySelector(
        '[aria-label="Prototype duration"]',
      ) as HTMLInputElement;
      expect(duration).not.toBeNull();
      duration.value = '475';
      duration.dispatchEvent(new Event('change'));
      await fixture.whenStable();
      await select('Prototype destination', 'frame-b');
      expect(saved.at(-1)?.patch.interaction).toEqual({
        trigger: 'click',
        action: 'navigate',
        target: 'frame-b',
        transition,
        durationMs: 475,
      });
    }
  });
  it('authors manual placement without losing the other coordinate or overlay options', async () => {
    const { fixture, saved, select } = await setup();
    await select('Prototype action', 'open-overlay');
    await select('Prototype transition', 'slide-up');
    await select('Overlay placement', 'manual');
    const x = fixture.nativeElement.querySelector(
      '[aria-label="Overlay offset X"]',
    ) as HTMLInputElement;
    const y = fixture.nativeElement.querySelector(
      '[aria-label="Overlay offset Y"]',
    ) as HTMLInputElement;
    x.value = '-40.5';
    x.dispatchEvent(new Event('change'));
    await fixture.whenStable();
    y.value = '92.25';
    y.dispatchEvent(new Event('change'));
    await fixture.whenStable();
    await select('Prototype destination', 'frame-b');
    expect(saved.at(-1)?.patch.interaction).toEqual({
      trigger: 'click',
      action: 'open-overlay',
      target: 'frame-b',
      transition: 'slide-up',
      durationMs: 300,
      placement: 'manual',
      offset: { x: -40.5, y: 92.25 },
    });
    await select('Overlay placement', 'bottom-right');
    expect(saved.at(-1)?.patch.interaction).toEqual({
      trigger: 'click',
      action: 'open-overlay',
      target: 'frame-b',
      transition: 'slide-up',
      durationMs: 300,
      placement: 'bottom-right',
    });
    expect(fixture.nativeElement.querySelector('[aria-label="Overlay offset X"]')).toBeNull();
    await select('Overlay placement', 'center');
    expect(saved.at(-1)?.patch.interaction).toEqual({
      trigger: 'click',
      action: 'open-overlay',
      target: 'frame-b',
      transition: 'slide-up',
      durationMs: 300,
    });
  });
  it('removes overlay-only options when navigating while retaining compatible slide settings', async () => {
    const { saved, select } = await setup(true, {
      id: 'copy',
      type: 'rect',
      inInstance: true,
      interaction: {
        trigger: 'click',
        action: 'open-overlay',
        target: 'frame-b',
        placement: 'manual',
        offset: { x: 4, y: 5 },
        dismissOutside: false,
        transition: 'slide-right',
        durationMs: 560,
      },
    } as NodeProps);
    await select('Prototype action', 'navigate');
    expect(saved.at(-1)).toEqual({
      id: 'copy',
      patch: {
        interaction: {
          trigger: 'click',
          action: 'navigate',
          target: 'frame-b',
          transition: 'slide-right',
          durationMs: 560,
        },
      },
    });
    await select('Prototype action', 'back');
    expect(saved.at(-1)?.patch.interaction).toEqual({ trigger: 'click', action: 'back' });
  });
  it('reflects late inherited placement and transition input without submitting edits', async () => {
    const { fixture, saved } = await setup();
    fixture.componentRef.setInput('node', {
      id: 'copy',
      type: 'rect',
      inInstance: true,
      interaction: {
        trigger: 'click',
        action: 'open-overlay',
        target: 'frame-b',
        placement: 'manual',
        offset: { x: 12.5, y: -9 },
        transition: 'slide-down',
        durationMs: 900,
      },
    } as NodeProps);
    fixture.componentRef.setInput('screens', []);
    await fixture.whenStable();
    fixture.componentRef.setInput('screens', screens);
    await fixture.whenStable();
    const control = (label: string) =>
      fixture.nativeElement.querySelector(`[aria-label="${label}"]`) as HTMLInputElement;
    expect(control('Prototype destination').value).toBe('frame-b');
    expect(control('Prototype transition').value).toBe('slide-down');
    expect(control('Prototype duration').value).toBe('900');
    expect(control('Overlay placement').value).toBe('manual');
    expect(control('Overlay offset X').value).toBe('12.5');
    expect(control('Overlay offset Y').value).toBe('-9');
    expect(saved).toEqual([]);
  });
  it('restores inherited manual placement and coordinates when edits are refused', async () => {
    const { fixture, saved, select } = await setup(false, {
      id: 'copy',
      type: 'rect',
      inInstance: true,
      interaction: {
        trigger: 'click',
        action: 'open-overlay',
        target: 'frame-b',
        placement: 'manual',
        offset: { x: 22, y: 33 },
        transition: 'slide-left',
        durationMs: 800,
      },
    } as NodeProps);
    expect((await select('Overlay placement', 'top-right')).value).toBe('manual');
    const x = fixture.nativeElement.querySelector(
      '[aria-label="Overlay offset X"]',
    ) as HTMLInputElement;
    x.value = '18';
    x.dispatchEvent(new Event('change'));
    await fixture.whenStable();
    expect(x.value).toBe('22');
    expect(saved.at(-1)?.id).toBe('copy');
    expect(
      (saved.at(-1)?.patch.interaction as { offset: { x: number; y: number } }).offset,
    ).toEqual({ x: 18, y: 33 });
    expect(fixture.nativeElement.querySelector('[role=alert]').textContent).toContain('refused');
  });
  it('rejects invalid manual offsets before saving and accepts finite fractional boundary values', async () => {
    const { fixture, saved, select } = await setup();
    await select('Prototype action', 'open-overlay');
    await select('Overlay placement', 'manual');
    const x = fixture.nativeElement.querySelector(
        '[aria-label="Overlay offset X"]',
      ) as HTMLInputElement,
      count = saved.length;
    for (const value of ['', '100000.01', '-100000.01', 'Infinity']) {
      x.value = value;
      x.dispatchEvent(new Event('change'));
      await fixture.whenStable();
      expect(x.value).toBe('0');
    }
    expect(saved.length).toBe(count);
    x.value = '-100000';
    x.dispatchEvent(new Event('change'));
    await fixture.whenStable();
    expect(
      (saved.at(-1)?.patch.interaction as { offset: { x: number; y: number } }).offset,
    ).toEqual({ x: -100000, y: 0 });
  });
});
