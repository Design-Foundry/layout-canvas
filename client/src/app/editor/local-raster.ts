export interface LocalRaster {
  readonly width: number;
  readonly height: number;
  readonly pixels: string;
  readonly id: string;
}

/** Inspect dimensions before handing a compressed image to the browser decoder. */
export function rasterDimensions(bytes: Uint8Array): {
  width: number;
  height: number;
  mime: string;
} {
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  let width = 0,
    height = 0,
    mime = '';
  if (
    bytes.length >= 33 &&
    [137, 80, 78, 71, 13, 10, 26, 10].every((v, i) => bytes[i] === v) &&
    view.getUint32(8) === 13 &&
    view.getUint32(12) === 0x49484452
  ) {
    width = view.getUint32(16);
    height = view.getUint32(20);
    mime = 'image/png';
    // APNG is deliberately excluded: one static decoded raster has no animation contract.
    for (let offset = 8; offset + 12 <= bytes.length; ) {
      const length = view.getUint32(offset),
        type = view.getUint32(offset + 4);
      if (type === 0x6163544c)
        throw new Error('Animated PNG is not supported. Choose a static PNG or JPEG.');
      if (length > bytes.length - offset - 12) throw new Error('Invalid PNG chunk.');
      offset += length + 12;
    }
  } else if (bytes.length >= 4 && bytes[0] === 255 && bytes[1] === 216 && bytes[2] === 255) {
    mime = 'image/jpeg';
    for (let offset = 2; offset + 4 <= bytes.length; ) {
      if (bytes[offset++] !== 255) throw new Error('Invalid JPEG marker.');
      while (bytes[offset] === 255) offset++;
      const marker = bytes[offset++];
      if (marker === 0xda || marker === 0xd9) break;
      if (marker === 0x01 || (marker >= 0xd0 && marker <= 0xd7)) continue;
      if (offset + 2 > bytes.length) break;
      const length = view.getUint16(offset);
      if (length < 2 || offset + length > bytes.length) throw new Error('Invalid JPEG segment.');
      if ([0xc0, 0xc1, 0xc2].includes(marker)) {
        if (length < 8) throw new Error('Invalid JPEG frame.');
        height = view.getUint16(offset + 3);
        width = view.getUint16(offset + 5);
        break;
      }
      offset += length;
    }
  }
  if (!mime || width < 1 || height < 1 || width > 4096 || height > 4096)
    throw new Error('Choose a static PNG or JPEG up to 4096 × 4096 pixels.');
  return { width, height, mime };
}

export async function importLocalRaster(file: File): Promise<LocalRaster> {
  if (file.size < 1 || file.size > 4 * 1024 * 1024)
    throw new Error('Choose a PNG or JPEG file up to 4 MB.');
  const bytes = new Uint8Array(await file.arrayBuffer()),
    header = rasterDimensions(bytes);
  const bitmap = await createImageBitmap(new Blob([bytes], { type: header.mime }));
  try {
    if (bitmap.width < 1 || bitmap.height < 1 || bitmap.width > 4096 || bitmap.height > 4096)
      throw new Error('Decoded image exceeds the size limit.');
    const scale = Math.min(1, 512 / Math.max(bitmap.width, bitmap.height));
    const width = Math.max(1, Math.round(bitmap.width * scale)),
      height = Math.max(1, Math.round(bitmap.height * scale));
    const canvas = new OffscreenCanvas(width, height),
      context = canvas.getContext('2d', { willReadFrequently: true });
    if (!context) throw new Error('Local image decoding is unavailable.');
    context.drawImage(bitmap, 0, 0, width, height);
    const pixels = context.getImageData(0, 0, width, height).data;
    const digestBytes = new Uint8Array(8 + pixels.length),
      prefix = new DataView(digestBytes.buffer);
    prefix.setUint32(0, width, true);
    prefix.setUint32(4, height, true);
    digestBytes.set(pixels, 8);
    const id = [...new Uint8Array(await crypto.subtle.digest('SHA-256', digestBytes))]
      .map((v) => v.toString(16).padStart(2, '0'))
      .join('');
    let binary = '';
    for (let i = 0; i < pixels.length; i += 32768)
      binary += String.fromCharCode(...pixels.subarray(i, i + 32768));
    return { width, height, pixels: btoa(binary), id };
  } finally {
    bitmap.close();
  }
}
