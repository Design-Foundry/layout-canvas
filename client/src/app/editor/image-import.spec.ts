import { TestBed } from '@angular/core/testing';
import { vi } from 'vitest';
import { ImageImport, LOCAL_RASTER_DECODER } from './image-import';
import * as raster from './local-raster';
const image = { width: 1, height: 1, pixels: 'AAAA/w==', id: 'a'.repeat(64) };
async function setup(accept = true, decode = vi.fn()) {
  await TestBed.configureTestingModule({
    imports: [ImageImport],
    providers: [{ provide: LOCAL_RASTER_DECODER, useValue: decode }],
  }).compileComponents();
  const fixture = TestBed.createComponent(ImageImport),
    add = vi.fn(() => accept);
  fixture.componentRef.setInput('add', add);
  await fixture.whenStable();
  const pick = () => {
    const input = fixture.nativeElement.querySelector('input') as HTMLInputElement;
    Object.defineProperty(input, 'files', {
      configurable: true,
      value: [new File(['pixels'], 'Photo.png', { type: 'image/png' })],
    });
    input.dispatchEvent(new Event('change'));
  };
  return { fixture, add, pick };
}
describe('local image import', () => {
  afterEach(() => vi.restoreAllMocks());
  it('imports decoded pixels and names the image from the file', async () => {
    const decode = vi.fn().mockResolvedValue(image);
    const { fixture, add, pick } = await setup(true, decode);
    pick();
    await fixture.whenStable();
    expect(decode).toHaveBeenCalledOnce();
    expect(add).toHaveBeenCalledWith(image, 'Photo.png');
    expect(fixture.nativeElement.textContent).toContain('512 px');
  });
  it('reports decode and native-budget refusals without adding invalid content', async () => {
    const decode = vi.fn().mockRejectedValue(new Error('Unsupported image'));
    const { fixture, add, pick } = await setup(false, decode);
    pick();
    await fixture.whenStable();
    expect(add).not.toHaveBeenCalled();
    expect(fixture.nativeElement.textContent).toContain('Unsupported image');
    decode.mockResolvedValue(image);
    pick();
    await fixture.whenStable();
    expect(add).toHaveBeenCalledOnce();
    expect(fixture.nativeElement.textContent).toContain('refused');
  });
  it('drops a pending decode when its panel is destroyed', async () => {
    let resolve!: (image: raster.LocalRaster) => void;
    const decode = vi.fn().mockImplementation(
      () =>
        new Promise((done) => {
          resolve = done;
        }),
    );
    const { fixture, add, pick } = await setup(true, decode);
    pick();
    fixture.destroy();
    resolve(image);
    await Promise.resolve();
    expect(add).not.toHaveBeenCalled();
  });
});
