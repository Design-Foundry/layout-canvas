const UNSAFE = /[\u0000-\u001f<>:"/\\|?*]+/g;
const MAX_NAME = 100;

export const EXPORT_SCALES = [1, 2, 3] as const;
export type ExportScale = (typeof EXPORT_SCALES)[number];

export function formatScale(scale: number): string {
  return String(Number(scale.toFixed(2)));
}

/** `<node name>@<scale>x.png` with characters that file systems reject replaced. */
export function exportFileName(name: string, scale: number): string {
  const base =
    name
      .replace(/\s+/g, ' ')
      .replace(UNSAFE, '-')
      .trim()
      .replace(/^\.+|[. ]+$/g, '')
      .slice(0, MAX_NAME)
      .trim() || 'Export';
  return `${base}@${formatScale(scale)}x.png`;
}

/** Names for several exports at once: repeats get a numeric suffix before the scale. */
export function createFileNamer(): (name: string, scale: number) => string {
  const used = new Map<string, number>();
  return (name, scale) => {
    const file = exportFileName(name, scale);
    const count = (used.get(file) ?? 0) + 1;
    used.set(file, count);
    return count === 1 ? file : exportFileName(`${name} ${count}`, scale);
  };
}

const CRC_TABLE = (() => {
  const table = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    table[n] = c >>> 0;
  }
  return table;
})();

function crc32(parts: readonly Uint8Array[]): number {
  let c = 0xffffffff;
  for (const part of parts) for (let i = 0; i < part.length; i++) c = CRC_TABLE[(c ^ part[i]) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}

function chunk(type: string, data: Uint8Array<ArrayBuffer>): Uint8Array<ArrayBuffer> {
  const out = new Uint8Array(12 + data.length);
  const view = new DataView(out.buffer);
  view.setUint32(0, data.length);
  for (let i = 0; i < 4; i++) out[4 + i] = type.charCodeAt(i);
  out.set(data, 8);
  view.setUint32(8 + data.length, crc32([out.subarray(4, 8), data]));
  return out;
}

function deflate(bytes: Uint8Array<ArrayBuffer>): ReadableStream<Uint8Array> {
  const source = new ReadableStream<Uint8Array<ArrayBuffer>>({
    start(controller) {
      controller.enqueue(bytes);
      controller.close();
    },
  });
  return source.pipeThrough(new CompressionStream('deflate'));
}

/** Encodes straight-alpha RGBA, rows top to bottom, as an 8-bit PNG. */
export async function encodePng(width: number, height: number, rgba: Uint8Array): Promise<Blob> {
  const stride = width * 4;
  const raw = new Uint8Array(height * (stride + 1));
  for (let y = 0; y < height; y++) raw.set(rgba.subarray(y * stride, (y + 1) * stride), y * (stride + 1) + 1);
  const compressed = new Uint8Array(
    await new Response(deflate(raw)).arrayBuffer(),
  );
  const header = new Uint8Array(13);
  const view = new DataView(header.buffer);
  view.setUint32(0, width);
  view.setUint32(4, height);
  header[8] = 8;
  header[9] = 6;
  const signature = new Uint8Array([137, 80, 78, 71, 13, 10, 26, 10]);
  return new Blob([signature, chunk('IHDR', header), chunk('IDAT', compressed), chunk('IEND', new Uint8Array(0))], {
    type: 'image/png',
  });
}
