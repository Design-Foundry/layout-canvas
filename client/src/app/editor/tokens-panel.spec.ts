import { TestBed } from '@angular/core/testing';
import { INTER_FONT_ID } from '@canvas/bridge';
import { FontService } from './font-service';
import { INTER, font, fontCore, stackOf } from './font-core.testing';
import { TokensPanel } from './tokens-panel';

async function setup(fonts = [INTER, font('lora', {}, { displayName: 'Lora' })]) {
  TestBed.configureTestingModule({ imports: [TokensPanel] });
  const core = fontCore(fonts, [stackOf('s', fonts[0].id)]);
  const service = TestBed.inject(FontService);
  service.attach(core as any);
  const fixture = TestBed.createComponent(TokensPanel);
  fixture.componentRef.setInput('tokens', []);
  fixture.componentRef.setInput('modes', [{ id: 'default', name: 'Default', order: 'a0', isDefault: true }]);
  fixture.componentRef.setInput('activeMode', 'default');
  await fixture.whenStable();
  return { fixture, core, el: fixture.nativeElement as HTMLElement };
}

describe('font availability', () => {
  it('lists the document fonts except Inter with an Available checkbox', async () => {
    const { el } = await setup();
    const rows = el.querySelectorAll('label.font');
    expect(rows).toHaveLength(1);
    expect(rows[0].textContent).toContain('Lora');
    const box = rows[0].querySelector('[aria-label="Available"]') as HTMLInputElement;
    expect(box.checked).toBe(true);
  });

  it('hides the section when only Inter exists', async () => {
    const { el } = await setup([INTER]);
    expect(el.querySelector('[aria-label="Fonts"]')).toBeNull();
  });

  it('marks a font unavailable and available again', async () => {
    const { fixture, core, el } = await setup();
    const box = () => el.querySelector('label.font input') as HTMLInputElement;
    box().click();
    await fixture.whenStable();
    expect(core.setFontAvailable).toHaveBeenLastCalledWith('lora', false);
    expect(box().checked).toBe(false);
    box().click();
    await fixture.whenStable();
    expect(core.setFontAvailable).toHaveBeenLastCalledWith('lora', true);
    expect(box().checked).toBe(true);
    expect(core.setFontAvailable).not.toHaveBeenCalledWith(INTER_FONT_ID, expect.anything());
  });
});
