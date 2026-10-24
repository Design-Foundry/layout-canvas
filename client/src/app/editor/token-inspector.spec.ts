import { TestBed } from '@angular/core/testing';
import { TokenInspector } from './token-inspector';
import type { TokenRow, TokenDef } from '@canvas/bridge';

describe('shared typography token authoring', () => {
  it('edits a complete reusable style without content or binding controls and preserves metadata', async () => {
    await TestBed.configureTestingModule({ imports: [TokenInspector] }).compileComponents();
    const fixture = TestBed.createComponent(TokenInspector),
      saved: TokenDef[] = [];
    const token: TokenRow = {
      id: 'style',
      type: 'typography',
      identifier: 'type.body',
      displayName: 'Body',
      order: 'a0',
      referenced: true,
      value: {
        font: 'inter-regular-v1',
        fontSize: 20,
        lineHeight: 1.4,
        letterSpacing: 0,
        align: 'left',
      },
    };
    fixture.componentRef.setInput('token', token);
    fixture.componentRef.setInput('tokens', [token]);
    fixture.componentRef.setInput('modes', [
      { id: 'default', name: 'Default', order: 'a0', isDefault: true },
    ]);
    fixture.componentRef.setInput('colorFromHex', () => null);
    fixture.componentRef.setInput('replace', (next: TokenDef) => {
      saved.push(next);
      return true;
    });
    await fixture.whenStable();
    expect(fixture.nativeElement.querySelector('[aria-label="Shared typography"]')).toBeNull();
    expect(fixture.nativeElement.querySelector('textarea[aria-label="Text content"]')).toBeNull();
    const select = fixture.nativeElement.querySelector(
      '[aria-label="Font style"]',
    ) as HTMLSelectElement;
    select.value = 'inter-bold-oblique-v1';
    select.dispatchEvent(new Event('change'));
    await fixture.whenStable();
    expect(saved.at(-1)).toEqual({
      id: 'style',
      type: 'typography',
      identifier: 'type.body',
      displayName: 'Body',
      order: 'a0',
      value: {
        font: 'inter-bold-oblique-v1',
        fontSize: 20,
        lineHeight: 1.4,
        letterSpacing: 0,
        align: 'left',
      },
    });
    fixture.componentRef.setInput('replace', () => false);
    await fixture.whenStable();
    select.value = 'inter-regular-v1';
    select.dispatchEvent(new Event('change'));
    await fixture.whenStable();
    expect(select.value).toBe('inter-bold-oblique-v1');
    expect(saved).toHaveLength(1);
  });
});
