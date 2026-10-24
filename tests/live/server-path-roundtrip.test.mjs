import assert from 'node:assert/strict';
import { test } from 'node:test';

const origin = new URL(process.env.CANVAS_TEST_URL || 'http://127.0.0.1:5180');
assert.ok(['127.0.0.1', 'localhost', '[::1]'].includes(origin.hostname));
const id = (n) => `00000000-0000-0000-0000-${String(n).padStart(12, '0')}`;
const request = (path, body, method = 'POST') =>
  fetch(new URL(path, origin), {
    method,
    headers: { 'Content-Type': 'application/json' },
    ...(body ? { body: JSON.stringify(body) } : {}),
  });
function fixture() {
  return {
    version: 13,
    modes: [{ id: id(1), name: 'Default', order: 'a0' }],
    tokens: [],
    nodes: [
      { id: id(10), type: 'component', order: 'a0', w: 240, h: 160 },
      {
        id: id(11),
        type: 'path',
        parent: id(10),
        order: 'a0',
        x: 20,
        y: 30,
        w: 160,
        h: 80,
        lineWidth: 10,
        path: {
          points: [
            { x: 0, y: 1 },
            { x: 0.5, y: 0 },
            { x: 1, y: 1 },
          ],
          closed: false,
        },
      },
      { id: id(12), type: 'instance', component: id(10), order: 'a1', w: 240, h: 160 },
    ],
  };
}

test('native paths retain their points and independent width overrides through HTTP storage', async () => {
  let documentId;
  try {
    const snapshot = fixture();
    const created = await request('/api/documents/import', { name: 'Native path QA', snapshot });
    const imported = await created.json();
    assert.equal(created.status, 201, JSON.stringify(imported));
    documentId = imported.id;
    const loaded = await (await fetch(new URL(`/api/documents/${documentId}`, origin))).json();
    assert.equal(loaded.snapshot.data.version, 13);
    assert.deepEqual(
      loaded.snapshot.data.nodes.find((node) => node.type === 'path').path,
      snapshot.nodes[1].path,
    );
    const append = async (kind, data) => {
      const response = await request(`/api/documents/${documentId}/ops`, {
        ops: [{ id: crypto.randomUUID(), kind, data }],
      });
      return { status: response.status, body: await response.json() };
    };
    let result = await append('SetOverride', { id: id(12), node: id(11), patch: { lineWidth: 4 } });
    assert.equal(result.status, 200);
    assert.ok(result.body.acks[0].seq);
    const changed = {
      points: [
        { x: 0, y: 0.5 },
        { x: 0.5, y: 0 },
        { x: 1, y: 1 },
      ],
      closed: true,
    };
    result = await append('SetProps', { id: id(11), patch: { path: changed } });
    assert.equal(result.status, 200);
    assert.ok(result.body.acks[0].seq);
    for (const [target, patch] of [
      [id(10), { path: changed }],
      [
        id(11),
        {
          path: {
            points: [
              { x: 0, y: 0 },
              { x: 2, y: 1 },
            ],
            closed: false,
          },
        },
      ],
      [id(11), { cornerRadius: 4 }],
    ]) {
      result = await append('SetProps', { id: target, patch });
      assert.ok(
        result.status === 400 || (result.status === 200 && result.body.acks[0].seq === null),
        JSON.stringify(result),
      );
    }
    const operations = await (
      await fetch(new URL(`/api/documents/${documentId}/ops?after=1`, origin))
    ).json();
    assert.equal(operations.ops.length, 2);
    assert.deepEqual(operations.ops[1].data.patch.path, changed);
  } finally {
    if (documentId)
      assert.equal((await request(`/api/documents/${documentId}`, null, 'DELETE')).status, 204);
  }
});

test('path imports refuse downgraded, oversized and invalid closed geometry without creating documents', async () => {
  const invalid = [];
  const older = fixture();
  older.version = 12;
  invalid.push(older);
  const excessive = fixture();
  excessive.nodes[1].path.points = Array.from({ length: 65 }, (_, index) => ({
    x: index / 64,
    y: 0,
  }));
  invalid.push(excessive);
  const closed = fixture();
  closed.nodes[1].path = {
    points: [
      { x: 0, y: 0 },
      { x: 1, y: 1 },
    ],
    closed: true,
  };
  invalid.push(closed);
  const unrelated = fixture();
  unrelated.nodes[0].path = unrelated.nodes[1].path;
  invalid.push(unrelated);
  for (const snapshot of invalid) {
    const response = await request('/api/documents/import', { name: 'Invalid path QA', snapshot });
    assert.equal(response.status, 400, await response.text());
  }
});
