import { TestBed } from '@angular/core/testing';
import { PrototypeControls } from './prototype-controls';

describe('accessible prototype controls', () => {
  it('announces the current screen and disables unavailable history without disabling exit during animation', async () => {
    await TestBed.configureTestingModule({ imports: [PrototypeControls] }).compileComponents();
    const fixture = TestBed.createComponent(PrototypeControls);
    fixture.componentRef.setInput('name', 'Checkout');
    fixture.componentRef.setInput('transitioning', true);
    await fixture.whenStable();
    const nav = fixture.nativeElement.querySelector('nav');
    expect(nav.getAttribute('aria-label')).toBe('Prototype presentation');
    expect(nav.getAttribute('aria-busy')).toBe('true');
    expect(fixture.nativeElement.querySelector('[role="status"]').textContent).toBe('Checkout');
    const buttons = fixture.nativeElement.querySelectorAll('button');
    expect(buttons[0].disabled).toBe(true);
    expect(buttons[2].disabled).toBe(false);
  });

  it('emits independent actions and exposes close only for an open overlay', async () => {
    await TestBed.configureTestingModule({ imports: [PrototypeControls] }).compileComponents();
    const fixture = TestBed.createComponent(PrototypeControls), actions: string[] = [];
    for (const action of ['back', 'restart', 'closeOverlay', 'exit'] as const)
      fixture.componentInstance[action].subscribe(() => actions.push(action));
    fixture.componentRef.setInput('canBack', true);
    fixture.componentRef.setInput('overlay', true);
    await fixture.whenStable();
    fixture.nativeElement.querySelectorAll('button').forEach((button: HTMLButtonElement) => button.click());
    expect(actions).toEqual(['back', 'restart', 'closeOverlay', 'exit']);
    fixture.componentRef.setInput('overlay', false);
    await fixture.whenStable();
    expect(fixture.nativeElement.textContent).not.toContain('Close overlay');
  });
});
