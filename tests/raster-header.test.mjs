import assert from 'node:assert/strict';
import { test } from 'node:test';
import { rasterDimensions } from '../client/src/app/editor/local-raster.ts';
function png(width, height) {
  const a = new Uint8Array(33);
  a.set([137, 80, 78, 71, 13, 10, 26, 10]);
  const v = new DataView(a.buffer);
  v.setUint32(8, 13);
  v.setUint32(12, 0x49484452);
  v.setUint32(16, width);
  v.setUint32(20, height);
  return a;
}
test('raster preflight accepts bounded PNG and baseline/progressive JPEG dimensions', () => {
  assert.deepEqual(rasterDimensions(png(100, 200)), { width: 100, height: 200, mime: 'image/png' });
  for (const marker of [0xc0, 0xc2])
    assert.deepEqual(
      rasterDimensions(new Uint8Array([255, 216, 255, marker, 0, 8, 8, 0, 12, 0, 24, 3])),
      { width: 24, height: 12, mime: 'image/jpeg' },
    );
});
test('raster preflight rejects oversized, zero-sized, truncated, animated and unrelated inputs', () => {
  const animated = new Uint8Array(45);
  animated.set(png(32, 32));
  new DataView(animated.buffer).setUint32(37, 0x6163544c);
  for (const bytes of [
    png(4097, 1),
    png(0, 1),
    new Uint8Array([255, 216, 255, 0xc0, 0, 255]),
    new TextEncoder().encode('<svg/>'),
    animated,
  ])
    assert.throws(() => rasterDimensions(bytes));
});
