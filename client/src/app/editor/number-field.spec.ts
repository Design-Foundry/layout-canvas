import { TestBed } from '@angular/core/testing';
import { NumberField } from './number-field';
async function setup() {
  await TestBed.configureTestingModule({ imports: [NumberField] }).compileComponents();
  const fixture = TestBed.createComponent(NumberField);
  const commits: (number | null)[] = [];
  fixture.componentRef.setInput('label', 'H');
  fixture.componentRef.setInput('value', 168);
  fixture.componentInstance.commit.subscribe((value) => commits.push(value));
  await fixture.whenStable();
  const field = fixture.nativeElement.querySelector('input') as HTMLInputElement;
  field.dispatchEvent(new FocusEvent('focusin', { bubbles: true }));
  await fixture.whenStable();
  return { fixture, field, commits };
}
describe('numeric inspector with computed layout values', () => {
  it('updates an untouched focused value without committing a phantom edit on blur', async () => {
    const { fixture, field, commits } = await setup();
    fixture.componentRef.setInput('value', 252);
    await fixture.whenStable();
    expect(field.value).toBe('252');
    field.dispatchEvent(new FocusEvent('focusout', { bubbles: true }));
    await fixture.whenStable();
    expect(commits).toEqual([]);
  });
  it('keeps a typed draft when a computed value changes and commits only the user value', async () => {
    const { fixture, field, commits } = await setup();
    field.value = '180';
    field.dispatchEvent(new Event('input', { bubbles: true }));
    fixture.componentRef.setInput('value', 252);
    await fixture.whenStable();
    expect(field.value).toBe('180');
    field.dispatchEvent(new FocusEvent('focusout', { bubbles: true }));
    expect(commits).toEqual([180]);
  });
  it('Escape restores the current computed value without creating an edit', async () => {
    const { fixture, field, commits } = await setup();
    field.value = '180';
    field.dispatchEvent(new Event('input', { bubbles: true }));
    fixture.componentRef.setInput('value', 252);
    await fixture.whenStable();
    field.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true }));
    field.dispatchEvent(new FocusEvent('focusout', { bubbles: true }));
    await fixture.whenStable();
    expect(field.value).toBe('252');
    expect(commits).toEqual([]);
  });
});
