import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { test } from 'node:test';
import { readComponent } from '../client/src/app/editor/component-package.ts';
const id = (n) => `00000000-0000-0000-0000-${String(n).padStart(12, '0')}`;
const raster = (width = 2, height = 1, salt = 0) => {
  const pixels = Buffer.alloc(width * height * 4, salt),
    prefix = Buffer.alloc(8);
  prefix.writeUInt32LE(width, 0);
  prefix.writeUInt32LE(height, 4);
  return {
    width,
    height,
    pixels: pixels.toString('base64'),
    id: createHash('sha256').update(prefix).update(pixels).digest('hex'),
  };
};
const fixture = () => ({
  format: 'canvas-component/1',
  name: 'Image',
  componentId: id(1),
  snapshot: {
    version: 15,
    modes: [],
    tokens: [],
    nodes: [
      { id: id(1), type: 'component', order: 'a0', w: 200, h: 100 },
      {
        id: id(2),
        type: 'image',
        parent: id(1),
        order: 'a0',
        w: 120,
        h: 80,
        image: raster(),
        imageFit: 'contain',
        imageCrop: { x: 0.25, y: 0, w: 0.75, h: 1 },
      },
    ],
  },
});
test('snapshot15 preserves local pixel payload, crop and fit', () => {
  const data = fixture(),
    parsed = readComponent(JSON.stringify(data));
  assert.deepEqual(
    parsed.snapshot.nodes.find((n) => n.type === 'image').image,
    data.snapshot.nodes[1].image,
  );
  data.snapshot.version = 14;
  assert.throws(() => readComponent(JSON.stringify(data)), /version 15/);
});
test('portable image boundaries reject malformed payloads and incompatible fields', () => {
  for (const fields of [
    { imageFit: 'tile' },
    { imageCrop: { x: 0.5, y: 0, w: 1, h: 1 } },
    { imageCrop: { x: 0, y: 0, w: 0, h: 1 } },
    { image: { ...raster(), width: 513 } },
    { image: { ...raster(), id: 'https://example.com/photo.png' } },
    { image: { ...raster(), pixels: 'invalid=' } },
    { image: { ...raster(), pixels: raster().pixels.slice(0, -1) + 'A' } },
  ]) {
    const data = fixture();
    Object.assign(data.snapshot.nodes[1], fields);
    assert.throws(() => readComponent(JSON.stringify(data)), /Invalid native image/);
  }
  const data = fixture();
  data.snapshot.nodes[0].imageFit = 'cover';
  assert.throws(() => readComponent(JSON.stringify(data)), /Invalid native image/);
});
test('image overrides require an image target and obey version15 and crop validation', () => {
  const data = fixture(),
    main = data.snapshot.nodes[0];
  main.definition = {
    axes: [{ name: 'fit', values: ['a', 'b'] }],
    rules: [
      {
        target: id(2),
        when: { fit: 'a' },
        patch: { imageFit: 'stretch', imageCrop: { x: 0, y: 0, w: 0.5, h: 1 } },
      },
    ],
  };
  assert.equal(
    readComponent(JSON.stringify(data)).snapshot.nodes[0].definition.rules[0].patch.imageFit,
    'stretch',
  );
  main.definition.rules[0].target = id(1);
  assert.throws(() => readComponent(JSON.stringify(data)), /must target native image/);
});
test('portable files enforce total embedded bytes and distinct-image limits', () => {
  const data = fixture(),
    template = data.snapshot.nodes[1];
  data.snapshot.nodes[1].image = raster(400, 400);
  data.snapshot.nodes.push({ ...template, id: id(3), order: 'a1', image: raster(400, 400, 1) });
  assert.throws(() => readComponent(JSON.stringify(data)), /image document budget/);
  const distinct = fixture();
  for (let i = 1; i < 9; i++)
    distinct.snapshot.nodes.push({
      ...distinct.snapshot.nodes[1],
      id: id(i + 2),
      order: `a${i}`,
      image: raster(1, 1, i),
    });
  assert.throws(() => readComponent(JSON.stringify(distinct)), /image document budget/);
});
