import { provideHttpClient } from '@angular/common/http';
import { HttpTestingController, provideHttpClientTesting } from '@angular/common/http/testing';
import { ComponentFixture, TestBed } from '@angular/core/testing';
import { provideRouter } from '@angular/router';
import { ComponentsPanel } from './components-panel';
import { exportComponent } from './component-package';

describe('ComponentsPanel', () => {
  let fixture: ComponentFixture<ComponentsPanel>;
  let http: HttpTestingController;
  const id = '00000000-0000-0000-0000-000000000010';
  const snapshot = () => ({
    version: 2,
    modes: [],
    tokens: [],
    nodes: [{ id, type: 'component', name: 'Button', order: 'a0', w: 120, h: 40 }],
  });
  const element = () => fixture.nativeElement as HTMLElement;

  beforeEach(async () => {
    await TestBed.configureTestingModule({
      imports: [ComponentsPanel],
      providers: [provideRouter([]), provideHttpClient(), provideHttpClientTesting()],
    }).compileComponents();
    fixture = TestBed.createComponent(ComponentsPanel);
    fixture.componentRef.setInput('layers', [
      { id, name: 'Button', kind: 'component', depth: 0, visible: true, locked: false },
      { id: 'shape', name: 'Rectangle', kind: 'rectangle', depth: 0, visible: true, locked: false },
      {
        id: 'copy',
        name: 'Instance copy',
        kind: 'component',
        depth: 1,
        visible: true,
        locked: false,
        inInstance: true,
      },
    ]);
    fixture.componentRef.setInput('snapshot', snapshot);
    http = TestBed.inject(HttpTestingController);
    await fixture.whenStable();
  });

  afterEach(() => http.verify());

  it('lists only main components and uses the selected main component', () => {
    const received: string[] = [];
    fixture.componentInstance.instantiate.subscribe((value) => received.push(value));
    const use = [...element().querySelectorAll('button')].find(
      (button) => button.textContent?.trim() === 'Use',
    )!;
    use.click();
    expect(received).toEqual([id]);
    expect(element().querySelector('article')?.textContent).toContain('Button');
    expect(element().querySelector('article')?.textContent).not.toContain('Rectangle');
    expect(element().querySelectorAll('article').length).toBe(1);
  });

  it('filters components by their name', async () => {
    const input = element().querySelector<HTMLInputElement>(
      'input[placeholder="Search components"]',
    )!;
    input.value = 'missing';
    input.dispatchEvent(new Event('input'));
    await fixture.whenStable();
    expect(element().querySelectorAll('article').length).toBe(0);
  });

  it('imports into a separate document and links a new tab without navigating away', async () => {
    const input = element().querySelector<HTMLInputElement>('input[type="file"]')!;
    const file = new File([JSON.stringify(exportComponent(snapshot(), id))], 'button.json', {
      type: 'application/json',
    });
    Object.defineProperty(input, 'files', { configurable: true, value: [file] });
    input.dispatchEvent(new Event('change'));
    await new Promise((resolve) => setTimeout(resolve, 0));
    const request = http.expectOne('/api/documents/import');
    expect(request.request.method).toBe('POST');
    expect(request.request.body.snapshot.nodes[0].id).toBe(id);
    request.flush({ id: 'new-document' });
    await new Promise((resolve) => setTimeout(resolve, 0));
    await fixture.whenStable();
    const link = element().querySelector<HTMLAnchorElement>('a[target="_blank"]')!;
    expect(link, element().textContent ?? '').not.toBeNull();
    expect(link.getAttribute('href')).toBe('/d/new-document');
    expect(link.rel).toBe('noopener');
  });

  it('reveals the main component from its library name', () => {
    const revealed: string[] = [];
    fixture.componentInstance.reveal.subscribe((value) => revealed.push(value));
    element().querySelector<HTMLButtonElement>('.component-name')!.click();
    expect(revealed).toEqual([id]);
  });
  it('rejects an unsupported native snapshot before starting an export', async () => {
    fixture.componentRef.setInput('snapshot', () => ({ ...snapshot(), version: 999 }));
    await fixture.whenStable();
    [...element().querySelectorAll('button')]
      .find((button) => button.textContent?.trim() === 'Export')!
      .click();
    await fixture.whenStable();
    expect(element().querySelector('[role="alert"]')?.textContent).toContain(
      'supported Canvas component snapshot',
    );
  });
});
