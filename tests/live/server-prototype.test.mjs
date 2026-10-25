import assert from 'node:assert/strict';
import { test } from 'node:test';
const origin = new URL(process.env.CANVAS_TEST_URL || 'http://127.0.0.1:5180');
assert.ok(['localhost', '127.0.0.1', '[::1]'].includes(origin.hostname));
const id = (n) => `00000000-0000-0000-0000-${String(n).padStart(12, '0')}`;
const navigate = (target) => ({ trigger: 'click', action: 'navigate', target });
const back = { trigger: 'click', action: 'back' };

test('snapshot18 persists directional transitions and manual modal placement with strict downgrade and option errors', async () => {
  for (const transition of [
    'slide-left',
    'slide-right',
    'slide-up',
    'slide-down',
    'push-left',
    'push-right',
    'push-up',
    'push-down',
  ]) {
    const snapshot = fixture();
    snapshot.version = 18;
    snapshot.nodes[1].interaction = { ...navigate(id(20)), transition, durationMs: 550 };
    await using(snapshot, async (document) => {
      const detail = await (await fetch(new URL(`/api/documents/${document}`, origin))).json();
      assert.equal(detail.snapshot.data.version, 18);
      assert.deepEqual(
        detail.snapshot.data.nodes.find((n) => n.id === id(11)).interaction,
        snapshot.nodes[1].interaction,
      );
    });
  }
  const snapshot = fixture();
  snapshot.version = 18;
  const open = {
    trigger: 'click',
    action: 'open-overlay',
    target: id(20),
    placement: 'manual',
    offset: { x: 28, y: 36 },
    transition: 'slide-up',
    durationMs: 600,
  };
  snapshot.nodes[1].interaction = open;
  await using(snapshot, async (document) => {
    const detail = await (await fetch(new URL(`/api/documents/${document}`, origin))).json();
    assert.deepEqual(detail.snapshot.data.nodes.find((n) => n.id === id(11)).interaction, open);
    for (const interaction of [
      { ...open, offset: { x: 100001, y: 0 } },
      { ...open, placement: 'top-left' },
      { ...open, transition: 'push-left' },
      { ...navigate(id(20)), placement: 'manual', offset: { x: 0, y: 0 } },
    ]) {
      refuse(await append(document, 'SetProps', { id: id(11), patch: { interaction } }));
    }
    accept(
      await append(document, 'SetOverride', {
        id: id(32),
        node: id(31),
        patch: { interaction: { ...open, placement: 'bottom-right', offset: undefined } },
      }),
    );
  });
  snapshot.version = 17;
  assert.equal(
    (await request('/api/documents/import', { name: 'Downlevel placement QA', snapshot })).status,
    400,
  );
});

test('snapshot17 persists dissolve and modal actions and refuses downgrade or malformed options', async () => {
  const snapshot = fixture();
  snapshot.version = 17;
  const open = {
    trigger: 'click',
    action: 'open-overlay',
    target: id(20),
    transition: 'dissolve',
    durationMs: 450,
    dismissOutside: false,
  };
  snapshot.nodes[1].interaction = open;
  snapshot.nodes.find((n) => n.id === id(21)).interaction = {
    trigger: 'click',
    action: 'close-overlay',
  };
  await using(snapshot, async (document) => {
    const detail = await (await fetch(new URL(`/api/documents/${document}`, origin))).json();
    assert.equal(detail.snapshot.data.version, 17);
    assert.deepEqual(detail.snapshot.data.nodes.find((n) => n.id === id(11)).interaction, open);
    refuse(await append(document, 'DeleteNode', { id: id(20) }));
    accept(
      await append(document, 'SetProps', {
        id: id(11),
        patch: { interaction: { ...navigate(id(20)), transition: 'dissolve', durationMs: 2000 } },
      }),
    );
    refuse(
      await append(document, 'SetProps', {
        id: id(11),
        patch: { interaction: { ...open, durationMs: 0 } },
      }),
    );
    refuse(
      await append(document, 'SetProps', {
        id: id(11),
        patch: { interaction: { ...back, dismissOutside: false } },
      }),
    );
  });
  snapshot.version = 16;
  assert.equal(
    (await request('/api/documents/import', { name: 'Downlevel modal QA', snapshot })).status,
    400,
  );
});
const request = (path, body, method = 'POST') =>
  fetch(new URL(path, origin), {
    method,
    headers: { 'Content-Type': 'application/json' },
    ...(body ? { body: JSON.stringify(body) } : {}),
  });
function fixture() {
  return {
    version: 16,
    modes: [],
    tokens: [],
    nodes: [
      {
        id: id(10),
        type: 'frame',
        name: 'Home',
        order: 'a0',
        w: 320,
        h: 240,
        prototypeStart: true,
      },
      {
        id: id(11),
        type: 'rect',
        parent: id(10),
        order: 'a0',
        x: 20,
        y: 20,
        w: 120,
        h: 40,
        interaction: navigate(id(20)),
      },
      { id: id(20), type: 'frame', name: 'Details', order: 'a1', x: 400, w: 320, h: 240 },
      {
        id: id(21),
        type: 'rect',
        parent: id(20),
        order: 'a0',
        x: 20,
        y: 20,
        w: 120,
        h: 40,
        interaction: back,
      },
      { id: id(30), type: 'component', name: 'Linked button', order: 'a2', w: 120, h: 40 },
      {
        id: id(31),
        type: 'rect',
        parent: id(30),
        order: 'a0',
        w: 120,
        h: 40,
        interaction: navigate(id(40)),
      },
      {
        id: id(32),
        type: 'instance',
        parent: id(10),
        component: id(30),
        order: 'a1',
        x: 20,
        y: 100,
        w: 120,
        h: 40,
      },
      { id: id(40), type: 'frame', name: 'Empty destination', order: 'a3', x: 800, w: 320, h: 240 },
    ],
  };
}
async function using(snapshot, work) {
  let document;
  try {
    const response = await request('/api/documents/import', {
        name: 'Prototype persistence QA',
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
const accept = (result) => {
  assert.equal(result.status, 200, JSON.stringify(result));
  assert.ok(result.body.acks[0].seq);
};
const refuse = (result) =>
  assert.ok(
    result.status === 400 || (result.status === 200 && result.body.acks[0].seq === null),
    JSON.stringify(result),
  );
test('snapshot16 retains forward prototype links, entry flags and independent copied interactions through HTTP', async () => {
  await using(fixture(), async (document) => {
    const detail = await (await fetch(new URL(`/api/documents/${document}`, origin))).json();
    assert.equal(detail.snapshot.data.version, 16);
    assert.deepEqual(
      detail.snapshot.data.nodes.find((n) => n.id === id(11)).interaction,
      navigate(id(20)),
    );
    assert.equal(detail.snapshot.data.nodes.find((n) => n.id === id(10)).prototypeStart, true);
    accept(
      await append(document, 'SetOverride', {
        id: id(32),
        node: id(31),
        patch: { interaction: null },
      }),
    );
    accept(await append(document, 'SetProps', { id: id(11), patch: { interaction: back } }));
    accept(await append(document, 'SetProps', { id: id(20), patch: { prototypeStart: true } }));
    const ops = await (
      await fetch(new URL(`/api/documents/${document}/ops?after=1`, origin))
    ).json();
    assert.equal(ops.ops.length, 3);
    assert.equal(ops.ops[0].data.patch.interaction, null);
  });
});
test('prototype references prevent deletion, nesting and promotion until inbound links are removed', async () => {
  await using(fixture(), async (document) => {
    refuse(await append(document, 'DeleteNode', { id: id(40) }));
    refuse(await append(document, 'MoveNode', { id: id(40), parent: id(10), order: 'a2' }));
    refuse(await append(document, 'SetProps', { id: id(40), patch: { type: 'component' } }));
    refuse(
      await append(document, 'SetProps', { id: id(11), patch: { interaction: navigate(id(999)) } }),
    );
    refuse(await append(document, 'SetProps', { id: id(11), patch: { prototypeStart: false } }));
    accept(
      await append(document, 'SetProps', { id: id(31), patch: { interaction: navigate(id(20)) } }),
    );
    accept(await append(document, 'DeleteNode', { id: id(40) }));
    const ops = await (
      await fetch(new URL(`/api/documents/${document}/ops?after=1`, origin))
    ).json();
    assert.equal(ops.ops.length, 2);
  });
});
test('prototype imports reject downgrades, invalid actions, non-screen destinations and misplaced flow starts', async () => {
  const invalid = [];
  const old = fixture();
  old.version = 15;
  invalid.push(old);
  const missing = fixture();
  missing.nodes[1].interaction = navigate(id(999));
  invalid.push(missing);
  const wrong = fixture();
  wrong.nodes[1].interaction = navigate(id(31));
  invalid.push(wrong);
  const extra = fixture();
  extra.nodes[1].interaction = { ...back, target: id(20) };
  invalid.push(extra);
  const hover = fixture();
  hover.nodes[1].interaction = { trigger: 'hover', action: 'back' };
  invalid.push(hover);
  const url = fixture();
  url.nodes[1].interaction = { trigger: 'click', action: 'open-url', url: 'https://example.test/' };
  invalid.push(url);
  const misplaced = fixture();
  misplaced.nodes[1].prototypeStart = true;
  invalid.push(misplaced);
  for (const snapshot of invalid) {
    const response = await request('/api/documents/import', {
      name: 'Invalid prototype QA',
      snapshot,
    });
    assert.equal(response.status, 400, await response.text());
  }
});
