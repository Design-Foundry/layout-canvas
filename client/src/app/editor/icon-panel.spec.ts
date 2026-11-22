import { provideHttpClient } from '@angular/common/http';
import { HttpTestingController, provideHttpClientTesting } from '@angular/common/http/testing';
import { TestBed } from '@angular/core/testing';
import type { NodePatch, NodeProps } from '@canvas/bridge';
import { IconPanel } from './icon-panel';

const SET = '00000000-0000-0000-0000-000000000200';
const SEARCH = '/api/icons/material-symbols/glyphs';
const icon = (axes?: Record<string, string | number>) => ({ set: SET, glyph: 'arrow_back', axes });

async function setup(save: (id: string, patch: NodePatch) => boolean, value = icon()) {
  await TestBed.configureTestingModule({
    imports: [IconPanel],
    providers: [provideHttpClient(), provideHttpClientTesting()],
  }).compileComponents();
  const fixture = TestBed.createComponent(IconPanel);
  fixture.componentRef.setInput('node', { id: 'icon', type: 'icon', icon: value } as NodeProps);
  fixture.componentRef.setInput('save', save);
  await fixture.whenStable();
  return { fixture, http: TestBed.inject(HttpTestingController), element: fixture.nativeElement as HTMLElement };
}
const field = (element: HTMLElement, label: string) => element.querySelector(`[aria-label="${label}"]`) as HTMLInputElement & HTMLSelectElement;

describe('icon inspector', () => {
  it('shows the glyph, style, fill and weight of the icon, defaults included', async () => {
    const { element } = await setup(() => true);
    expect(field(element, 'Glyph').value).toBe('arrow_back');
    expect(field(element, 'Style').value).toBe('outlined');
    expect(field(element, 'Filled').checked).toBe(false);
    expect(field(element, 'Weight').value).toBe('400');
    expect([...field(element, 'Style').options].map((o) => o.value)).toEqual(['outlined', 'rounded', 'sharp']);
    expect([...field(element, 'Weight').options].map((o) => o.value)).toEqual(['100', '200', '300', '400', '500', '600', '700']);
  });

  it('reads the axes the icon sets', async () => {
    const { element } = await setup(() => true, icon({ style: 'sharp', FILL: 1, wght: 700 }));
    expect(field(element, 'Style').value).toBe('sharp');
    expect(field(element, 'Filled').checked).toBe(true);
    expect(field(element, 'Weight').value).toBe('700');
  });

  it('saves one icon patch per change, keeping the other axes', async () => {
    const saved: [string, NodePatch][] = [];
    const { fixture, element } = await setup((id, patch) => (saved.push([id, patch]), true), icon({ wght: 300 }));

    const style = field(element, 'Style');
    style.value = 'rounded';
    style.dispatchEvent(new Event('change'));
    const filled = field(element, 'Filled');
    filled.checked = true;
    filled.dispatchEvent(new Event('change'));
    const weight = field(element, 'Weight');
    weight.value = '500';
    weight.dispatchEvent(new Event('change'));
    await fixture.whenStable();

    expect(saved).toEqual([
      ['icon', { icon: { set: SET, glyph: 'arrow_back', axes: { wght: 300, style: 'rounded' } } }],
      ['icon', { icon: { set: SET, glyph: 'arrow_back', axes: { wght: 300, FILL: 1 } } }],
      ['icon', { icon: { set: SET, glyph: 'arrow_back', axes: { wght: 500 } } }],
    ]);
  });

  it('puts a refused change back and says so', async () => {
    const { fixture, element } = await setup(() => false);
    const style = field(element, 'Style');
    style.value = 'sharp';
    style.dispatchEvent(new Event('change'));
    await fixture.whenStable();

    expect(style.value).toBe('outlined');
    expect(element.querySelector('[role="alert"]')).not.toBeNull();
  });

  it('suggests glyphs while typing and saves the one picked', async () => {
    const saved: NodePatch[] = [];
    const { fixture, http, element } = await setup((_id, patch) => (saved.push(patch), true), icon({ style: 'rounded' }));
    const input = field(element, 'Glyph');

    input.dispatchEvent(new Event('focus'));
    http.expectOne((r) => r.url === SEARCH && r.params.get('query') === '').flush({ total: 2, items: [{ glyph: 'add' }, { glyph: 'arrow_back' }] });
    await fixture.whenStable();
    expect([...element.querySelectorAll('[role="option"] button')].map((b) => b.textContent?.trim())).toEqual(['add', 'arrow_back']);

    input.value = 'home';
    input.dispatchEvent(new Event('input'));
    await new Promise((resolve) => setTimeout(resolve, 320));
    http.expectOne((r) => r.url === SEARCH && r.params.get('query') === 'home').flush({ total: 1, items: [{ glyph: 'home' }] });
    await fixture.whenStable();
    await new Promise((resolve) => setTimeout(resolve));
    fixture.detectChanges();
    (element.querySelector('[role="option"] button') as HTMLButtonElement).click();
    await fixture.whenStable();

    expect(saved).toEqual([{ icon: { set: SET, glyph: 'home', axes: { style: 'rounded' } } }]);
    expect(element.querySelector('[role="listbox"]')).toBeNull();
  });

  it('picks the exact match, or else the first suggestion, on Enter', async () => {
    const saved: NodePatch[] = [];
    const { fixture, http, element } = await setup((_id, patch) => (saved.push(patch), true));
    const input = field(element, 'Glyph');
    input.dispatchEvent(new Event('focus'));
    http.expectOne((r) => r.url === SEARCH).flush({ total: 3, items: [{ glyph: 'home' }, { glyph: 'home_work' }, { glyph: 'search' }] });
    await fixture.whenStable();

    input.value = 'Home Work';
    input.dispatchEvent(new Event('input'));
    input.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter' }));
    await fixture.whenStable();
    expect(saved.at(-1)).toEqual({ icon: { set: SET, glyph: 'home_work' } });
  });

  it('closes the suggestions on blur without changing the icon', async () => {
    const saved: NodePatch[] = [];
    const { fixture, http, element } = await setup((_id, patch) => (saved.push(patch), true));
    const input = field(element, 'Glyph');
    input.dispatchEvent(new Event('focus'));
    http.expectOne((r) => r.url === SEARCH).flush({ total: 1, items: [{ glyph: 'home' }] });
    await fixture.whenStable();
    input.dispatchEvent(new Event('blur'));
    await fixture.whenStable();

    expect(element.querySelector('[role="listbox"]')).toBeNull();
    expect(saved).toEqual([]);
    expect(input.value).toBe('arrow_back');
  });
});
