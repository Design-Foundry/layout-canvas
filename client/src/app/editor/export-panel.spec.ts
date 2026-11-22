import { TestBed } from '@angular/core/testing';
import type { ExportedImage } from '@canvas/bridge';
import { ExportPanel } from './export-panel';
import { createFileNamer, encodePng, exportFileName, formatScale } from './export-file';

describe('export file names', () => {
  it('names the file after the node and scale', () => {
    expect(exportFileName('Home screen', 2)).toBe('Home screen@2x.png');
    expect(exportFileName('Card', 1)).toBe('Card@1x.png');
  });

  it('replaces characters file systems reject', () => {
    expect(exportFileName('a/b\\c:d*e?f"g<h>i|j', 3)).toBe('a-b-c-d-e-f-g-h-i-j@3x.png');
    expect(exportFileName('  Header \n  row  ', 1)).toBe('Header row@1x.png');
    expect(exportFileName('..hidden.', 1)).toBe('hidden@1x.png');
  });

  it('falls back when nothing usable is left and bounds the length', () => {
    expect(exportFileName('', 1)).toBe('Export@1x.png');
    expect(exportFileName('///', 2)).toBe('Export@2x.png'.replace('Export', '-'));
    expect(exportFileName('x'.repeat(300), 1)).toBe(`${'x'.repeat(100)}@1x.png`);
  });

  it('formats fractional scales and keeps repeated names apart', () => {
    expect(formatScale(1.8000000001)).toBe('1.8');
    expect(exportFileName('Card', 1.75)).toBe('Card@1.75x.png');
    const name = createFileNamer();
    expect([name('Card', 2), name('Card', 2), name('Card', 2), name('Card', 1)]).toEqual([
      'Card@2x.png',
      'Card 2@2x.png',
      'Card 3@2x.png',
      'Card@1x.png',
    ]);
  });
});

describe('file name collisions', () => {
  it('skips names taken by an earlier suffix and ignores case', () => {
    const name = createFileNamer();
    expect([name('Card', 1), name('Card 2', 1), name('Card', 1), name('card', 1)]).toEqual([
      'Card@1x.png',
      'Card 2@1x.png',
      'Card 3@1x.png',
      'card 4@1x.png',
    ]);
  });

  it('avoids reserved Windows device names', () => {
    expect(exportFileName('CON', 1)).toBe('CON-@1x.png');
  });
});

describe('PNG encoding', () => {
  it('writes a valid PNG that keeps straight alpha', async () => {
    const rgba = new Uint8Array([255, 0, 0, 255, 0, 0, 255, 0, 10, 20, 30, 128, 1, 2, 3, 4]);
    const bytes = new Uint8Array(await (await encodePng(2, 2, rgba)).arrayBuffer());
    expect(Array.from(bytes.subarray(0, 8))).toEqual([137, 80, 78, 71, 13, 10, 26, 10]);
    const view = new DataView(bytes.buffer);
    expect(view.getUint32(16)).toBe(2);
    expect(view.getUint32(20)).toBe(2);
    expect(bytes[24]).toBe(8);
    expect(bytes[25]).toBe(6);
    const inflated = new Uint8Array(
      await new Response(
        new ReadableStream<Uint8Array<ArrayBuffer>>({
          start(controller) {
            controller.enqueue(bytes.slice(41, bytes.length - 16));
            controller.close();
          },
        }).pipeThrough(new DecompressionStream('deflate')),
      ).arrayBuffer(),
    );
    expect(Array.from(inflated)).toEqual([0, 255, 0, 0, 255, 0, 0, 255, 0, 0, 10, 20, 30, 128, 1, 2, 3, 4]);
    expect(new TextDecoder().decode(bytes.subarray(bytes.length - 8, bytes.length - 4))).toBe('IEND');
  });
});

describe('export panel', () => {
  const image = (overrides: Partial<ExportedImage> = {}): ExportedImage => ({
    width: 1,
    height: 1,
    scale: 1,
    requestedScale: 1,
    clamped: false,
    tiled: false,
    pixels: new Uint8Array([1, 2, 3, 255]),
    ...overrides,
  });

  async function setup(
    targets: { id: string; name: string }[],
    render: (id: string, scale: number) => Promise<ExportedImage>,
  ) {
    await TestBed.configureTestingModule({ imports: [ExportPanel] }).compileComponents();
    const fixture = TestBed.createComponent(ExportPanel);
    const saved: string[] = [];
    fixture.componentRef.setInput('targets', targets);
    fixture.componentRef.setInput('render', render);
    fixture.componentRef.setInput('save', (_blob: Blob, fileName: string) => saved.push(fileName));
    await fixture.whenStable();
    const root = fixture.nativeElement as HTMLElement;
    const button = () => root.querySelector('lucent-button button') as HTMLButtonElement;
    const choose = async (label: string) => {
      const option = [...root.querySelectorAll('lucent-segmented-control button')].find(
        (candidate) => candidate.textContent?.trim() === label,
      ) as HTMLButtonElement;
      option.click();
      await fixture.whenStable();
    };
    const run = async () => {
      button().click();
      await vi.waitFor(() => {
        fixture.detectChanges();
        expect(button().disabled).toBe(false);
        expect(root.querySelector('[role="status"], [role="alert"]') || saved.length).toBeTruthy();
      });
    };
    return { fixture, root, saved, choose, run, button };
  }

  it('offers 1x, 2x and 3x and an export button', async () => {
    const { root, button } = await setup([{ id: 'a', name: 'Card' }], async () => image());
    const labels = [...root.querySelectorAll('lucent-segmented-control button')].map((b) => b.textContent?.trim());
    expect(labels).toEqual(['1x', '2x', '3x']);
    expect(button().textContent?.trim()).toBe('Export');
  });

  it('downloads the selected node at the chosen scale', async () => {
    const calls: [string, number][] = [];
    const { saved, choose, run } = await setup([{ id: 'a', name: 'Home/Main' }], async (id, scale) => {
      calls.push([id, scale]);
      return image({ scale, requestedScale: scale });
    });
    await choose('3x');
    await run();
    expect(calls).toEqual([['a', 3]]);
    expect(saved).toEqual(['Home-Main@3x.png']);
  });

  it('exports every selected node to its own file', async () => {
    const { saved, run, button } = await setup(
      [
        { id: 'a', name: 'Card' },
        { id: 'b', name: 'Card' },
        { id: 'c', name: 'Header' },
      ],
      async () => image(),
    );
    expect(button().textContent?.trim()).toBe('Export 3 layers');
    await run();
    expect(saved).toEqual(['Card@1x.png', 'Card 2@1x.png', 'Header@1x.png']);
  });

  it('reports a clamped scale and names the file with the scale used', async () => {
    const { root, saved, choose, run } = await setup([{ id: 'a', name: 'Wide' }], async () =>
      image({ scale: 1.6, requestedScale: 3, clamped: true }),
    );
    await choose('3x');
    await run();
    expect(saved).toEqual(['Wide@1.6x.png']);
    expect(root.querySelector('[role="status"]')?.textContent).toContain('scale reduced to 1.6x');
  });

  it('reports tiled renders', async () => {
    const { root, run } = await setup([{ id: 'a', name: 'Tall' }], async () => image({ tiled: true }));
    await run();
    expect(root.querySelector('[role="status"]')?.textContent).toContain('rendered in tiles');
  });

  it('shows the failure and saves nothing', async () => {
    const { root, saved, run } = await setup([{ id: 'a', name: 'Card' }], async () => {
      throw new Error('node not found');
    });
    await run();
    expect(saved).toEqual([]);
    expect(root.querySelector('[role="alert"]')?.textContent).toContain('node not found');
  });
});
