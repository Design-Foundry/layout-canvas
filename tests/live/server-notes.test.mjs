import assert from 'node:assert/strict';
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
const snapshot = (version, notes) => ({
  version,
  modes: [],
  tokens: [],
  nodes: [{ id: id(1), type: 'frame', order: 'a0', name: 'Card', w: 100, h: 50, ...(notes ? { notes } : {}) }],
});

async function using(body, work) {
  const response = await request('/api/documents/import', { name: 'Notes QA', snapshot: body });
  const result = await response.json();
  assert.equal(response.status, 201, JSON.stringify(result));
  try {
    await work(result.id);
  } finally {
    assert.equal((await request(`/api/documents/${result.id}`, null, 'DELETE')).status, 204);
  }
}
const stored = async (document) =>
  (await (await fetch(new URL(`/api/documents/${document}`, origin))).json()).snapshot.data;

const append = async (document, patch) => {
  const response = await request(`/api/documents/${document}/ops`, {
    ops: [{ id: crypto.randomUUID(), kind: 'SetProps', data: { id: id(1), patch } }],
  });
  return { status: response.status, body: await response.json() };
};
const ops = async (document) =>
  (await (await fetch(new URL(`/api/documents/${document}/ops?after=1`, origin))).json()).ops;

test('notes persist through import, reopening, SetProps and clearing as snapshot 26', async () => {
  const text = 'Ask legal' + String.fromCharCode(10) + 'about the copy';
  await using(snapshot(26, text), async (document) => {
    const data = await stored(document);
    assert.equal(data.version, 26);
    assert.equal(data.nodes[0].notes, text);
    await using(data, async (reopened) => assert.equal((await stored(reopened)).nodes[0].notes, text));

    assert.equal((await append(document, { notes: 'Second' })).status, 200);
    assert.equal((await append(document, { notes: '' })).status, 200);
    assert.deepEqual((await ops(document)).map((op) => op.data.patch), [{ notes: 'Second' }, { notes: '' }]);
  });
});

test('SetProps refuses notes beyond the limit and leaves the log unchanged', async () => {
  await using(snapshot(26), async (document) => {
    const refused = await append(document, { notes: 'a'.repeat(2049) });
    assert.ok(refused.status >= 400 || refused.body.seq == null, JSON.stringify(refused));
    assert.equal((await ops(document)).length, 0);
  });
});

test('older snapshots load without notes and refuse them', async () => {
  await using(snapshot(25), async (document) => {
    assert.equal((await stored(document)).nodes[0].notes, undefined);
  });
  const refused = await request('/api/documents/import', { name: 'Notes QA', snapshot: snapshot(25, 'x') });
  assert.equal(refused.status, 400);
  assert.match(JSON.stringify(await refused.json()), /notes need snapshot version 26/);
});

test('notes beyond the limit are refused', async () => {
  const refused = await request('/api/documents/import', {
    name: 'Notes QA',
    snapshot: snapshot(26, 'a'.repeat(2049)),
  });
  assert.equal(refused.status, 400);
});
