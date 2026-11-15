import { TestBed } from '@angular/core/testing';
import { TokenInspector } from './token-inspector';
import { FontService } from './font-service';
import { fontCore } from './font-core.testing';
import { INTER_STACK_ID } from '@canvas/bridge';
import type { TokenRow, TokenDef } from '@canvas/bridge';

describe('shared typography token authoring', () => {
  it('edits a complete reusable style without content or binding controls and preserves metadata', async () => {
    await TestBed.configureTestingModule({ imports: [TokenInspector] }).compileComponents();
    TestBed.inject(FontService).attach(fontCore() as any);
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
        stack: INTER_STACK_ID,
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
      '[aria-label="Weight"]',
    ) as HTMLSelectElement;
    select.value = '700';
    select.dispatchEvent(new Event('change'));
    await fixture.whenStable();
    expect(saved.at(-1)).toEqual({
      id: 'style',
      type: 'typography',
      identifier: 'type.body',
      displayName: 'Body',
      order: 'a0',
      value: {
        stack: INTER_STACK_ID,
        axes: { wght: 700 },
        features: {},
        fontSize: 20,
        lineHeight: 1.4,
        letterSpacing: 0,
        align: 'left',
      },
    });
    fixture.componentRef.setInput('replace', () => false);
    await fixture.whenStable();
    select.value = '400';
    select.dispatchEvent(new Event('change'));
    await fixture.whenStable();
    expect(select.value).toBe('700');
    expect(saved).toHaveLength(1);
  });
});
