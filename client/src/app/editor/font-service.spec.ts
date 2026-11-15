import { provideHttpClient } from '@angular/common/http';
import { HttpTestingController, provideHttpClientTesting } from '@angular/common/http/testing';
import { TestBed } from '@angular/core/testing';
import { FontService, orderAfter } from './font-service';

const font = (id: string, key: string, order = 'a0') => ({
  id,
  identifier: key.toLowerCase(),
  displayName: key,
  provider: 'googlefonts',
  key,
  familyVersion: 'abc',
  capabilities: { axes: [], features: [] },
  order,
});
const manifest = [
  { face: 'face-1', sha256: 'aa', file: 'Roboto[wdth,wght].ttf' },
  { face: 'face-2', fixed: { ital: 1 }, sha256: 'bb', file: 'Roboto-Italic.ttf' },
];

function setup(initial = [font('f1', 'Roboto')]) {
  TestBed.configureTestingModule({ providers: [provideHttpClient(), provideHttpClientTesting()] });
  const core = {
    fonts: vi.fn(() => ({ fonts: initial, fontStacks: [] as any[] })),
    addFont: vi.fn(() => true),
    addFontStack: vi.fn(() => true),
    registerFontFamily: vi.fn((): string | null => null),
    registerFontFace: vi.fn((): string | null => null),
  };
  return { core, service: TestBed.inject(FontService), http: TestBed.inject(HttpTestingController) };
}
const flush = () => new Promise((resolve) => setTimeout(resolve));

describe('font service loading', () => {
  it('registers the family then every face once, skipping a checksum mismatch', async () => {
    const { core, service, http } = setup();
    vi.spyOn(console, 'warn').mockImplementation(() => {});
    service.attach(core);
    service.refresh();
    const url = '/api/fonts/googlefonts/abc/Roboto/files';
    http.expectOne(url).flush(manifest);
    await flush();
    expect(core.registerFontFamily).toHaveBeenCalledWith('googlefonts', 'Roboto', 'abc', [
      { face: 'face-1', fixed: undefined },
      { face: 'face-2', fixed: { ital: 1 } },
    ]);
    http
      .expectOne(`${url}/${encodeURIComponent('Roboto[wdth,wght].ttf')}`)
      .flush(new ArrayBuffer(4), { headers: { 'X-Font-SHA256': 'aa' } });
    http
      .expectOne(`${url}/Roboto-Italic.ttf`)
      .flush(new ArrayBuffer(4), { headers: { 'X-Font-SHA256': 'other' } });
    await flush();
    expect(core.registerFontFace).toHaveBeenCalledTimes(1);
    expect(core.registerFontFace).toHaveBeenCalledWith('face-1', expect.any(Uint8Array), 'aa');
    service.refresh();
    http.expectNone(url);
    http.verify();
  });
});

describe('font service picking', () => {
  it('searches the families endpoint', async () => {
    const { service, http } = setup();
    const result = service.search('rob');
    const request = http.expectOne((r) => r.url === '/api/fonts/google/families');
    expect(request.request.params.get('query')).toBe('rob');
    request.flush({ version: 'abc', families: [{ family: 'Roboto' }] });
    expect(await result).toEqual(['Roboto']);
  });

  it('reuses the font and stack the document already has', async () => {
    const { core, service } = setup();
    core.fonts.mockReturnValue({
      fonts: [font('f1', 'Roboto')],
      fontStacks: [{ id: 's1', entries: ['f1'] }] as any[],
    });
    service.attach(core);
    expect(await service.use('Roboto')).toBe('s1');
    expect(core.addFont).not.toHaveBeenCalled();
    expect(core.addFontStack).not.toHaveBeenCalled();
  });

  it('pins a new family, then adds its font and one-font stack', async () => {
    const { core, service, http } = setup([]);
    service.attach(core);
    core.fonts.mockReturnValue({ fonts: [font('i', 'Inter', 'a3')], fontStacks: [] });
    const { id: _id, order: _order, ...pinned } = font('x', 'Lora');
    const result = service.use('Lora');
    const request = http.expectOne('/api/fonts/google/pin');
    expect(request.request.body).toEqual({ family: 'Lora' });
    request.flush({ font: pinned, genericFallback: 'serif' });
    const stack = await result;
    const added = (core.addFont.mock.calls[0] as any[])[0];
    expect(added).toMatchObject({ key: 'Lora', order: 'a4' });
    expect((core.addFontStack.mock.calls[0] as any[])[0]).toMatchObject({
      id: stack,
      entries: [added.id],
      genericFallback: 'serif',
      displayName: 'Lora',
    });
  });
});

describe('orderAfter', () => {
  it('sorts after the previous key', () => {
    expect(orderAfter(undefined)).toBe('a0');
    expect(orderAfter('a0')).toBe('a1');
    expect(orderAfter('az')).toBe('b00');
    expect(orderAfter('a0V') > 'a0V').toBe(true);
  });
});
