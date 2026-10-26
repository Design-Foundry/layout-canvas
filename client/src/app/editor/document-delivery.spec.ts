import { provideHttpClient } from '@angular/common/http';
import { HttpTestingController, provideHttpClientTesting } from '@angular/common/http/testing';
import { ComponentFixture, TestBed } from '@angular/core/testing';
import { Router, provideRouter } from '@angular/router';
import { vi } from 'vitest';
import { DocumentDelivery } from './document-delivery';
import { exportDocument, MAX_DOCUMENT_FILE_BYTES } from './document-package';

describe('DocumentDelivery', () => {
  let fixture: ComponentFixture<DocumentDelivery>;
  let http: HttpTestingController;
  const snapshot = () => ({
    version: 22,
    modes: [],
    tokens: [],
    nodes: [
      {
        id: '00000000-0000-0000-0000-000000000001',
        type: 'frame',
        order: 'a0',
        prototypeStart: true,
        prototypeFlowName: 'Checkout',
      },
    ],
  });
  const element = () => fixture.nativeElement as HTMLElement;
  const choose = async (file: File) => {
    const input = element().querySelector<HTMLInputElement>('input[type="file"]')!;
    Object.defineProperty(input, 'files', { configurable: true, value: [file] });
    input.dispatchEvent(new Event('change'));
    await new Promise((resolve) => setTimeout(resolve, 0));
  };

  beforeEach(async () => {
    await TestBed.configureTestingModule({
      imports: [DocumentDelivery],
      providers: [provideRouter([]), provideHttpClient(), provideHttpClientTesting()],
    }).compileComponents();
    fixture = TestBed.createComponent(DocumentDelivery);
    fixture.componentRef.setInput('snapshot', snapshot);
    fixture.componentRef.setInput('name', 'Checkout');
    http = TestBed.inject(HttpTestingController);
    await fixture.whenStable();
  });
  afterEach(() => http.verify());

  it('downloads the current whole native snapshot without making an HTTP request', async () => {
    let blob: Blob | undefined;
    const create = vi.spyOn(URL, 'createObjectURL').mockImplementation((value) => {
      blob = value as Blob;
      return 'blob:document';
    });
    const revoke = vi.spyOn(URL, 'revokeObjectURL').mockImplementation(() => {});
    const click = vi.spyOn(HTMLAnchorElement.prototype, 'click').mockImplementation(() => {});
    element().querySelector<HTMLButtonElement>('button')!.click();
    expect(await blob!.text()).toBe(JSON.stringify(exportDocument(snapshot(), 'Checkout')));
    expect(revoke).toHaveBeenCalledWith('blob:document');
    expect((click.mock.instances[0] as HTMLAnchorElement).download).toBe('Checkout.canvas.json');
    create.mockRestore();
    revoke.mockRestore();
    click.mockRestore();
  });

  it('reopens through native server validation into a separate document without navigating away', async () => {
    const navigate = vi.spyOn(TestBed.inject(Router), 'navigate');
    await choose(
      new File([JSON.stringify(exportDocument(snapshot(), 'Checkout'))], 'demo.canvas.json'),
    );
    const request = http.expectOne('/api/documents/import');
    expect(request.request.body).toEqual({ name: 'Checkout', snapshot: snapshot() });
    request.flush({ id: 'imported' });
    await new Promise((resolve) => setTimeout(resolve, 0));
    await fixture.whenStable();
    const link = element().querySelector<HTMLAnchorElement>('a')!;
    expect(link.getAttribute('href')).toBe('/d/imported');
    expect(link.target).toBe('_blank');
    expect(link.rel).toBe('noopener');
    expect(navigate).not.toHaveBeenCalled();
    expect(element().textContent).toContain('configured Canvas server');
    navigate.mockRestore();
  });

  it('displays native validation diagnostics and keeps the active document intact', async () => {
    await choose(
      new File([JSON.stringify(exportDocument(snapshot(), 'Demo'))], 'demo.canvas.json'),
    );
    http
      .expectOne('/api/documents/import')
      .flush(
        { errors: { 'snapshot.nodes[0]': ['Invalid screen. Nothing was imported.'] } },
        { status: 400, statusText: 'Bad Request' },
      );
    await new Promise((resolve) => setTimeout(resolve, 0));
    await fixture.whenStable();
    expect(element().querySelector('[role="alert"]')?.textContent).toContain('Invalid screen');
    expect(element().querySelector('a')).toBeNull();
  });

  it('refuses oversized files before reading or sending them', async () => {
    const file = new File(['{}'], 'large.canvas.json');
    Object.defineProperty(file, 'size', { value: MAX_DOCUMENT_FILE_BYTES + 1 });
    const text = vi.spyOn(file, 'text');
    await choose(file);
    await fixture.whenStable();
    expect(text).not.toHaveBeenCalled();
    expect(element().querySelector('[role="alert"]')?.textContent).toContain('2 MB');
  });

  it('refuses a component package before issuing an import request', async () => {
    await choose(
      new File(
        [JSON.stringify({ format: 'canvas-component/1', name: 'Demo', snapshot: snapshot() })],
        'component.json',
      ),
    );
    await fixture.whenStable();
    expect(element().querySelector('[role="alert"]')?.textContent).toContain(
      'complete Canvas document',
    );
  });

  it('does not create a download for an unsupported snapshot', async () => {
    fixture.componentRef.setInput('snapshot', () => ({ ...snapshot(), version: 999 }));
    await fixture.whenStable();
    element().querySelector<HTMLButtonElement>('button')!.click();
    await fixture.whenStable();
    expect(element().querySelector('[role="alert"]')?.textContent).toContain(
      'supported Canvas document',
    );
  });
});
