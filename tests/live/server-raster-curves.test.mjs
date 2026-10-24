import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { test } from 'node:test';
const origin = new URL(process.env.CANVAS_TEST_URL || 'http://127.0.0.1:5180');
assert.ok(['localhost', '127.0.0.1', '[::1]'].includes(origin.hostname));
const id = (n) => `00000000-0000-0000-0000-${String(n).padStart(12, '0')}`;
const request = (path, body, method = 'POST') =>
  fetch(new URL(path, origin), {
    method,
    headers: { 'Content-Type': 'application/json' },
    ...(body ? { body: JSON.stringify(body) } : {}),
  });
const raster = (value = 255) => {
  const pixels = Buffer.from([value, 32, 128, 255, 32, 128, 255, 128]),
    prefix = Buffer.alloc(8);
  prefix.writeUInt32LE(2, 0);
  prefix.writeUInt32LE(1, 4);
  return {
    width: 2,
    height: 1,
    pixels: pixels.toString('base64'),
    id: createHash('sha256').update(prefix).update(pixels).digest('hex'),
  };
};
const fixture = (type, fields, version) => ({
  version,
  modes: [],
  tokens: [],
  nodes: [
    { id: id(1), type: 'component', order: 'a0', w: 240, h: 160 },
    { id: id(2), type, parent: id(1), order: 'a0', x: 10, y: 20, w: 200, h: 120, ...fields },
    { id: id(3), type: 'instance', component: id(1), order: 'a1', w: 240, h: 160 },
  ],
});
async function using(snapshot, work) {
  let document;
  try {
    const response = await request('/api/documents/import', {
        name: 'Isolated curves/raster QA',
        snapshot,
      }),
      result = await response.json();
    assert.equal(response.status, 201, JSON.stringify(result));
    document = result.id;
    await work(document);
  } finally {
    if (document)
      assert.equal((await request(`/api/documents/${document}`, null, 'DELETE')).status, 204);
  }
}
async function append(document, kind, data) {
  const response = await request(`/api/documents/${document}/ops`, {
    ops: [{ id: crypto.randomUUID(), kind, data }],
  });
  return { status: response.status, body: await response.json() };
}
const accepted = (result) => {
  assert.equal(result.status, 200, JSON.stringify(result));
  assert.ok(result.body.acks[0].seq);
};
const refused = (result) =>
  assert.ok(
    result.status === 400 || (result.status === 200 && result.body.acks[0].seq === null),
    JSON.stringify(result),
  );
test('snapshot14 preserves cubic controls and filled geometry through source edits and instance overrides', async () => {
  const path = {
    points: [
      { x: 0, y: 0.5 },
      { x: 0.5, y: 0 },
      { x: 1, y: 0.5 },
    ],
    closed: true,
    filled: true,
    inHandles: [
      { x: 0, y: 1 },
      { x: 0, y: 0 },
      { x: 1, y: 0 },
    ],
    outHandles: [
      { x: 0, y: 0 },
      { x: 1, y: 0 },
      { x: 1, y: 1 },
    ],
  };
  const snapshot = fixture('path', { path, lineWidth: 4 }, 14);
  await using(snapshot, async (document) => {
    const loaded = await (await fetch(new URL(`/api/documents/${document}`, origin))).json();
    assert.equal(loaded.snapshot.data.version, 14);
    assert.deepEqual(loaded.snapshot.data.nodes.find((n) => n.type === 'path').path, path);
    accepted(
      await append(document, 'SetOverride', { id: id(3), node: id(2), patch: { lineWidth: 8 } }),
    );
    accepted(
      await append(document, 'SetProps', {
        id: id(2),
        patch: { path: { ...path, filled: false } },
      }),
    );
    refused(
      await append(document, 'SetProps', {
        id: id(2),
        patch: { path: { ...path, inHandles: [] } },
      }),
    );
    refused(
      await append(document, 'SetProps', {
        id: id(2),
        patch: { path: { ...path, closed: false } },
      }),
    );
  });
  snapshot.version = 13;
  const old = await request('/api/documents/import', { name: 'Invalid cubic downgrade', snapshot });
  assert.equal(old.status, 400, await old.text());
});
test('snapshot15 retains verified local raster pixels, independent crop and inherited image identity', async () => {
  const image = raster(),
    snapshot = fixture(
      'image',
      { image, imageFit: 'contain', imageCrop: { x: 0, y: 0, w: 1, h: 1 } },
      15,
    );
  await using(snapshot, async (document) => {
    const loaded = await (await fetch(new URL(`/api/documents/${document}`, origin))).json();
    assert.equal(loaded.snapshot.data.version, 15);
    assert.deepEqual(loaded.snapshot.data.nodes.find((n) => n.type === 'image').image, image);
    accepted(
      await append(document, 'SetOverride', {
        id: id(3),
        node: id(2),
        patch: { imageCrop: { x: 0.5, y: 0, w: 0.5, h: 1 }, imageFit: 'cover' },
      }),
    );
    accepted(
      await append(document, 'SetProps', {
        id: id(2),
        patch: { image: raster(128), opacity: 0.6 },
      }),
    );
    for (const patch of [
      { imageCrop: { x: 0.5, y: 0, w: 1, h: 1 } },
      { imageFit: 'remote' },
      { image: { ...image, id: '0'.repeat(64) } },
      { image: { ...image, pixels: 'https://example.test/image.png' } },
      { cornerRadius: 4 },
    ])
      refused(await append(document, 'SetProps', { id: id(2), patch }));
    refused(await append(document, 'SetProps', { id: id(1), patch: { image } }));
    const ops = await (
      await fetch(new URL(`/api/documents/${document}/ops?after=1`, origin))
    ).json();
    assert.equal(ops.ops.length, 2);
  });
  snapshot.version = 14;
  const old = await request('/api/documents/import', {
    name: 'Invalid raster downgrade',
    snapshot,
  });
  assert.equal(old.status, 400, await old.text());
});
test('raster asset limit refuses a ninth identity without recording a partial operation', async () => {
  const snapshot = {
    version: 15,
    modes: [],
    tokens: [],
    nodes: Array.from({ length: 8 }, (_, i) => ({
      id: id(10 + i),
      type: 'image',
      order: `a${i}`,
      w: 2,
      h: 1,
      image: raster(i),
    })),
  };
  await using(snapshot, async (document) => {
    refused(
      await append(document, 'CreateNode', {
        node: { id: id(20), type: 'image', order: 'b0', w: 2, h: 1, image: raster(9) },
      }),
    );
    const ops = await (
      await fetch(new URL(`/api/documents/${document}/ops?after=1`, origin))
    ).json();
    assert.equal(ops.ops.length, 0);
  });
});
