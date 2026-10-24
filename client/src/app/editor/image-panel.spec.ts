import { TestBed } from '@angular/core/testing';
import { ImagePanel, validImageCrop } from './image-panel';
import type { NodePatch, NodeProps } from '@canvas/bridge';
const source = {
  id: 'image',
  type: 'image',
  x: 20,
  y: 30,
  w: 100,
  h: 80,
  box: { x: 20, y: 30, w: 100, h: 80 },
  image: { id: 'a'.repeat(64), width: 2, height: 1, pixels: 'AAAA/wAAAP8=' },
  imageFit: 'cover',
  imageCrop: { x: 0, y: 0, w: 1, h: 1 },
} as unknown as NodeProps;
async function setup(accept = true) {
  await TestBed.configureTestingModule({ imports: [ImagePanel] }).compileComponents();
  const fixture = TestBed.createComponent(ImagePanel),
    saved: NodePatch[] = [];
  fixture.componentRef.setInput('node', source);
  fixture.componentRef.setInput('save', (_id: string, patch: NodePatch) => {
    saved.push(patch);
    return accept;
  });
  await fixture.whenStable();
  const change = async (label: string, value: string) => {
    const field = [...fixture.nativeElement.querySelectorAll('app-number-field')].find(
      (f: any) => f.textContent.trim() === label,
    ) as HTMLElement;
    const input = field.querySelector('input')!;
    input.dispatchEvent(new FocusEvent('focusin', { bubbles: true }));
    input.value = value;
    input.dispatchEvent(new Event('input', { bubbles: true }));
    input.dispatchEvent(new FocusEvent('focusout', { bubbles: true }));
    await fixture.whenStable();
  };
  return { fixture, saved, change };
}
describe('native image inspector', () => {
  it('validates a source region atomically and rejects empty or out-of-image crops', () => {
    expect(validImageCrop({ x: 0.25, y: 0.25, w: 0.75, h: 0.75 })).toBe(true);
    expect(validImageCrop({ x: 0.25, y: 0, w: 1, h: 1 })).toBe(false);
    expect(validImageCrop({ x: 0, y: 0, w: 0, h: 1 })).toBe(false);
  });
  it('sets full crop atomically while preserving image pixels and fit independently', async () => {
    const { fixture, saved, change } = await setup();
    await change('Crop W', '0.5');
    await change('Crop X', '0.25');
    expect(saved.at(-1)).toEqual({ imageCrop: { x: 0.25, y: 0, w: 0.5, h: 1 } });
    const fit = fixture.nativeElement.querySelector('select') as HTMLSelectElement;
    fit.value = 'contain';
    fit.dispatchEvent(new Event('change'));
    await fixture.whenStable();
    expect(saved.at(-1)).toEqual({ imageFit: 'contain' });
    await change('W', '150');
    expect(saved.at(-1)).toEqual({ w: 150, widthSizing: 'fixed' });
    expect(fixture.nativeElement.textContent).toContain('2 × 1 embedded pixels');
  });
  it('rejects invalid and refused crop edits and resets to the full source', async () => {
    const { fixture, saved, change } = await setup();
    await change('Crop X', '0.5');
    expect(saved).toHaveLength(0);
    expect(fixture.nativeElement.querySelector('[role=alert]')).not.toBeNull();
    await change('Crop W', '0.5');
    fixture.nativeElement.querySelector('button').click();
    await fixture.whenStable();
    expect(saved.at(-1)).toEqual({ imageCrop: { x: 0, y: 0, w: 1, h: 1 } });
  });
  it('restores a refused fit selection and keeps auto-layout positions locked', async () => {
    const { fixture, saved } = await setup(false);
    const select = fixture.nativeElement.querySelector('select') as HTMLSelectElement;
    select.value = 'stretch';
    select.dispatchEvent(new Event('change'));
    await fixture.whenStable();
    expect(select.value).toBe('cover');
    expect(saved).toHaveLength(1);
    fixture.componentRef.setInput('node', { ...source, inFlow: true });
    await fixture.whenStable();
    expect(fixture.nativeElement.querySelector('app-number-field input').readOnly).toBe(true);
  });
});
