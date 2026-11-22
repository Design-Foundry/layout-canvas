import assert from 'node:assert/strict';
import { test } from 'node:test';
const origin = new URL(process.env.CANVAS_TEST_URL || 'http://127.0.0.1:5180');
assert.ok(['localhost', '127.0.0.1', '[::1]'].includes(origin.hostname));
const id = (n) => `00000000-0000-0000-0000-${String(n).padStart(12, '0')}`;
const SET = '00000000-0000-0000-0000-000000000200';
const request = (path, body, method = 'POST') =>
  fetch(new URL(path, origin), {
    method,
    headers: { 'Content-Type': 'application/json' },
    ...(body ? { body: JSON.stringify(body) } : {}),
  });
const snapshot = (version, icon) => ({
  version,
  modes: [],
  tokens: [],
  nodes: [{ id: id(1), type: 'icon', order: 'a0', name: 'Back', icon }],
});

async function using(body, work) {
  const response = await request('/api/documents/import', { name: 'Icons QA', snapshot: body });
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

test('icons persist as snapshot 27 in the structured form and round trip through reopening', async () => {
  await using(snapshot(27, 'material-symbols/arrow_back?style=rounded&fill=1&wght=500'), async (document) => {
    const data = await stored(document);
    assert.equal(data.version, 27);
    assert.equal(data.iconSets[0].id, SET);
    assert.equal(data.iconSets[0].identifier, 'material-symbols');
    assert.deepEqual(data.nodes[0].icon, { set: SET, glyph: 'arrow_back', axes: { style: 'rounded', FILL: 1, wght: 500 } });
    assert.equal(data.nodes[0].w, 24);
    assert.equal(data.nodes[0].h, 24);
    await using(data, async (reopened) => assert.deepEqual((await stored(reopened)).nodes[0].icon, data.nodes[0].icon));
  });
});

test('SetProps swaps the glyph, logs the structured form and refuses values the set does not offer', async () => {
  await using(snapshot(27, 'material-symbols/home'), async (document) => {
    assert.equal((await append(document, { icon: 'material-symbols/settings?wght=300' })).status, 200);
    const bad = await append(document, { icon: 'material-symbols/settings?wght=450' });
    assert.ok(bad.status >= 400 || bad.body.acks?.[0]?.error, JSON.stringify(bad));
    const logged = await ops(document);
    assert.equal(logged.length, 1);
    assert.deepEqual(logged[0].data.patch.icon, { set: SET, glyph: 'settings', axes: { wght: 300 } });
  });
});

test('older snapshots refuse icons', async () => {
  const refused = await request('/api/documents/import', {
    name: 'Icons QA',
    snapshot: snapshot(26, 'material-symbols/home'),
  });
  assert.equal(refused.status, 400);
  assert.match(JSON.stringify(await refused.json()), /icon layers need snapshot version 27/);
});

test('the glyph endpoint refuses axis values the source does not have without reaching GitHub', async () => {
  for (const query of ['style=bold', 'wght=450', 'FILL=2', 'opsz=30']) {
    const response = await fetch(new URL(`/api/icons/material-symbols/glyphs/home?${query}`, origin));
    assert.equal(response.status, 400, query);
  }
  assert.equal((await fetch(new URL('/api/icons/material-symbols/glyphs/Home', origin))).status, 400);
});
