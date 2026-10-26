import assert from 'node:assert/strict';
import { test } from 'node:test';
import { createHash } from 'node:crypto';
import { exportDocument, readDocument } from '../../client/src/app/editor/document-package.ts';
const origin = new URL(process.env.CANVAS_TEST_URL || 'http://127.0.0.1:5180');
assert.ok(['localhost', '127.0.0.1', '[::1]'].includes(origin.hostname));
const id = (n) => `00000000-0000-0000-0000-${String(n).padStart(12, '0')}`;
const navigate = (target) => ({ trigger: 'click', action: 'navigate', target });
const back = { trigger: 'click', action: 'back' };

function deliveryFixture() {
  const snapshot = fixture();
  snapshot.version = 22;
  snapshot.nodes[0].prototypeFlowName = 'Checkout';
  snapshot.nodes[0].prototypeOverflow = 'vertical';
  snapshot.nodes.find((node) => node.id === id(30)).definition = {
    properties: [], axes: [{ name: 'state', values: ['idle', 'active'], default: 'idle' }], rules: [],
  };
  snapshot.nodes.find((node) => node.id === id(31)).interaction = {
    trigger: 'click', action: 'change-state', values: { state: 'active' },
    transition: 'smart-animate', durationMs: 700,
  };
  const dimensions = Buffer.alloc(8);
  dimensions.writeUInt32LE(1, 0); dimensions.writeUInt32LE(1, 4);
  const rgba = Buffer.from([255, 120, 0, 255]);
  snapshot.nodes.push({
    id: id(50), type: 'image', parent: id(20), order: 'a1', w: 24, h: 24,
    image: { id: createHash('sha256').update(dimensions).update(rgba).digest('hex'),
      width: 1, height: 1, pixels: rgba.toString('base64') },
  });
  return snapshot;
}

test('whole-document local package reopens native flows, links, states and inline images faithfully', async () => {
  const source = deliveryFixture();
  const bundle = readDocument(JSON.stringify(exportDocument(source, 'Local flow delivery')));
  await using(bundle.snapshot, async (document) => {
    const detail = await (await fetch(new URL(`/api/documents/${document}`, origin))).json();
    const stored = detail.snapshot.data;
    assert.equal(stored.version, 22);
    assert.equal(stored.nodes.find((node) => node.id === id(10)).prototypeFlowName, 'Checkout');
    assert.equal(stored.nodes.find((node) => node.id === id(10)).prototypeOverflow, 'vertical');
    assert.deepEqual(stored.nodes.find((node) => node.id === id(11)).interaction, navigate(id(20)));
    assert.deepEqual(stored.nodes.find((node) => node.id === id(31)).interaction,
      source.nodes.find((node) => node.id === id(31)).interaction);
    assert.deepEqual(stored.nodes.find((node) => node.id === id(50)).image, source.nodes.at(-1).image);
    const delivered = readDocument(JSON.stringify(exportDocument(stored, 'Reopened')));
    await using(delivered.snapshot, async (reopened) => {
      assert.notEqual(reopened, document);
      const fresh = await (await fetch(new URL(`/api/documents/${reopened}`, origin))).json();
      assert.deepEqual(fresh.snapshot.data, stored);
      assert.equal(fresh.headSeq, 1);
    });
    const unchanged = await (await fetch(new URL(`/api/documents/${document}`, origin))).json();
    assert.deepEqual(unchanged, detail, 'opening a delivery copy never changes the current document');
  });
});

test('native local reopen rejects bad destinations and corrupt inline image content atomically', async () => {
  await using(fixture(), async (document) => {
    const before = await (await fetch(new URL(`/api/documents/${document}`, origin))).json();
    const listBefore = await (await fetch(new URL('/api/documents', origin))).json();
    for (const corrupt of ['target', 'pixels']) {
      const invalid = deliveryFixture();
      if (corrupt === 'target') invalid.nodes[1].interaction.target = id(999);
      else invalid.nodes.at(-1).image.pixels = 'AAAAAA==';
      // Envelope parsing deliberately delegates native semantics and asset hashes to visualcore.
      const bundle = readDocument(JSON.stringify(exportDocument(invalid, 'Invalid local delivery')));
      const response = await request('/api/documents/import', { name: bundle.name, snapshot: bundle.snapshot });
      assert.equal(response.status, 400, await response.text());
    }
    const after = await (await fetch(new URL(`/api/documents/${document}`, origin))).json();
    const listAfter = await (await fetch(new URL('/api/documents', origin))).json();
    assert.deepEqual(after, before);
    assert.deepEqual(listAfter, listBefore, 'invalid imports must not create partial documents');
  });
});

test('snapshot21 preserves relative component states, Smart Animate and independent overrides', async () => {
  const snapshot=fixture();snapshot.version=21;
  const state={trigger:'click',action:'change-state',values:{state:'active'},transition:'smart-animate',durationMs:700};
  snapshot.nodes.find(n=>n.id===id(30)).definition={properties:[],axes:[{name:'state',values:['idle','active'],default:'idle'}],rules:[]};
  snapshot.nodes.find(n=>n.id===id(31)).interaction=state;
  snapshot.nodes.find(n=>n.id===id(32)).overrides=[{node:id(31),patch:{interactions:[{...state,trigger:'hover'}]}}];
  await using(snapshot,async(document)=>{
    const detail=await (await fetch(new URL(`/api/documents/${document}`,origin))).json();
    assert.equal(detail.snapshot.data.version,21);
    assert.deepEqual(detail.snapshot.data.nodes.find(n=>n.id===id(31)).interaction,state);
    assert.deepEqual(detail.snapshot.data.nodes.find(n=>n.id===id(32)).overrides[0].patch.interactions,[{...state,trigger:'hover'}]);
    accept(await append(document,'SetProps',{id:id(11),patch:{interaction:{...navigate(id(20)),transition:'smart-animate',durationMs:500}}}));
    accept(await append(document,'SetOverride',{id:id(32),node:id(31),patch:{interaction:{...state,values:{state:'idle'},transition:'dissolve'}}}));
    const result=await (await fetch(new URL(`/api/documents/${document}/ops?after=1`,origin))).json();
    assert.equal(result.ops.length,2);
    assert.equal(result.ops[0].data.patch.interaction.transition,'smart-animate');
    assert.deepEqual(result.ops[1].data.patch.interaction.values,{state:'idle'});
  });
});

test('state actions reject malformed values, unsupported options and snapshot21 downgrades', async () => {
  const state={trigger:'click',action:'change-state',values:{state:'active'}};
  await using(fixture(),async(document)=>{
    for(const interaction of [
      {...state,target:id(20)}, {...state,values:{}}, {...state,values:{state:true}},
      {...state,values:{'':'active'}}, {...state,values:{state:''}},
      {...state,values:{['x'.repeat(65)]:'active'}}, {...state,values:{state:'x'.repeat(129)}},
      {...state,values:Object.fromEntries(Array.from({length:9},(_,n)=>[`axis${n}`,'active']))},
      {...state,transition:'slide-left'}, {...state,transition:'push-left'},
      {...state,transition:'instant',durationMs:100},
      {...navigate(id(20)),values:{state:'active'}},
      {trigger:'click',action:'open-overlay',target:id(20),transition:'smart-animate'},
    ]) refuse(await append(document,'SetProps',{id:id(31),patch:{interaction}}));
    const ops=await (await fetch(new URL(`/api/documents/${document}/ops?after=1`,origin))).json();
    assert.equal(ops.ops.length,0);
  });
  for(const overrideOnly of [false,true]) {
    const snapshot=fixture();snapshot.version=20;
    if(overrideOnly) snapshot.nodes.find(n=>n.id===id(32)).overrides=[{node:id(31),patch:{interaction:state}}];
    else snapshot.nodes.find(n=>n.id===id(31)).interaction=state;
    const response=await request('/api/documents/import',{name:'Invalid state downgrade QA',snapshot});
    assert.equal(response.status,400,await response.text());
  }
});

test('snapshot20 preserves ordered triggers and independent copied lists through HTTP', async () => {
  const snapshot = fixture();
  snapshot.version = 20;
  const interactions = [
    { trigger: 'hover', action: 'open-overlay', target: id(20), placement: 'top-right' },
    { trigger: 'press', action: 'navigate', target: id(40), transition: 'slide-left', durationMs: 240 },
    { trigger: 'delay', action: 'navigate', target: id(20), delayMs: 1200 },
    back,
  ];
  delete snapshot.nodes[1].interaction;
  snapshot.nodes[1].interactions = interactions;
  snapshot.nodes.find((n) => n.id === id(32)).overrides = [
    { node: id(31), patch: { interactions: [{ trigger: 'press', action: 'back' }] } },
  ];
  await using(snapshot, async (document) => {
    const detail = await (await fetch(new URL(`/api/documents/${document}`, origin))).json();
    assert.equal(detail.snapshot.data.version, 20);
    const source = detail.snapshot.data.nodes.find((n) => n.id === id(11));
    assert.deepEqual(source.interactions, interactions);
    assert.equal(source.interaction, undefined);
    const copied = detail.snapshot.data.nodes.find((n) => n.id === id(32)).overrides;
    assert.equal(copied.length, 1);
    assert.equal(copied[0].node, id(31));
    assert.deepEqual(copied[0].patch.interactions, [{ trigger: 'press', action: 'back' }]);
    assert.ok(copied[0].patch.interaction == null, 'the list masks the legacy inherited action');
    accept(await append(document, 'SetProps', { id: id(11), patch: { interactions: [...interactions].reverse() } }));
    accept(await append(document, 'SetOverride', { id: id(32), node: id(31), patch: { interactions: [] } }));
    accept(await append(document, 'SetProps', { id: id(11), patch: { interaction: navigate(id(20)) } }));
    accept(await append(document, 'SetProps', { id: id(11), patch: { interactions: [back] } }));
    const result = await (await fetch(new URL(`/api/documents/${document}/ops?after=1`, origin))).json();
    assert.equal(result.ops.length, 4);
    assert.deepEqual(result.ops.map((op) => op.data.patch), [
      { interactions: [...interactions].reverse() },
      { interactions: [] },
      { interaction: navigate(id(20)) },
      { interactions: [back] },
    ]);
  });
});

test('ordered lists validate every destination and reject malformed trigger edits without persisting them', async () => {
  const snapshot = fixture();
  snapshot.version = 20;
  delete snapshot.nodes[1].interaction;
  snapshot.nodes[1].interactions = [back, { trigger: 'hover', action: 'navigate', target: id(40) }];
  snapshot.nodes.find((n) => n.id === id(31)).interaction = back;
  await using(snapshot, async (document) => {
    refuse(await append(document, 'DeleteNode', { id: id(40) }));
    refuse(await append(document, 'MoveNode', { id: id(40), parent: id(10), order: 'a2' }));
    refuse(await append(document, 'SetProps', { id: id(40), patch: { type: 'component' } }));
    const invalid = [
      null, {}, [null], Array.from({ length: 17 }, () => back),
      [{ trigger: 'hover', action: 'none' }],
      [{ trigger: 'double-click', action: 'back' }],
      [{ action: 'back' }],
      [{ trigger: 'delay', action: 'back' }],
      ...[0, 60001, 1.5, '100', null].map((delayMs) => [{ trigger: 'delay', action: 'back', delayMs }]),
      [{ ...back, delayMs: 100 }],
      [{ ...back, unexpected: true }],
      [{ trigger: 'press', action: 'navigate', target: id(999) }],
      [{ trigger: 'hover', action: 'open-overlay', target: id(31) }],
      [{ trigger: 'delay', action: 'open-overlay', target: id(20), delayMs: 100, transition: 'push-left' }],
    ];
    for (const interactions of invalid)
      refuse(await append(document, 'SetProps', { id: id(11), patch: { interactions } }));
    refuse(await append(document, 'SetProps', { id: id(11), patch: { interaction: back, interactions: [back] } }));
    refuse(await append(document, 'SetProps', { id: id(32), patch: { interactions: [] } }));
    refuse(await append(document, 'SetComponentDefinition', {
      id: id(30),
      definition: {
        axes: [{ name: 'state', values: ['a', 'b'], default: 'a' }],
        rules: [{ when: { state: 'b' }, target: id(31), patch: { interactions: [back] } }],
      },
    }));
    accept(await append(document, 'SetProps', { id: id(11), patch: { interactions: [] } }));
    accept(await append(document, 'DeleteNode', { id: id(40) }));
    const result = await (await fetch(new URL(`/api/documents/${document}/ops?after=1`, origin))).json();
    assert.equal(result.ops.length, 2, 'refused list changes cannot enter the oplog');
    assert.deepEqual(result.ops.map((op) => op.kind), ['SetProps', 'DeleteNode']);
  });
});

test('snapshot20 lists reject downlevel or misplaced metadata including explicit empty override lists', async () => {
  const list = fixture();
  list.version = 20;
  delete list.nodes[1].interaction;
  list.nodes[1].interactions = [{ trigger: 'delay', action: 'back', delayMs: 1 }];
  const invalid = [];
  for (const metadata of [list.nodes[1].interactions, []]) {
    const downlevel = structuredClone(list);
    downlevel.version = 19;
    downlevel.nodes[1].interactions = metadata;
    invalid.push(downlevel);
  }
  const oldOverride = fixture();
  oldOverride.version = 19;
  oldOverride.nodes.find((n) => n.id === id(32)).overrides = [{ node: id(31), patch: { interactions: [] } }];
  invalid.push(oldOverride);
  const rawInstance = structuredClone(list);
  rawInstance.nodes.find((n) => n.id === id(32)).interactions = [];
  invalid.push(rawInstance);
  const duplicate = structuredClone(list);
  duplicate.nodes[1].interaction = back;
  invalid.push(duplicate);
  for (const snapshot of invalid) {
    const response = await request('/api/documents/import', { name: 'Invalid trigger QA', snapshot });
    assert.equal(response.status, 400, await response.text());
  }
});

test('snapshot19 persists screen and overlay overflow while rejecting downgrades and invalid edits outside the oplog', async () => {
  const snapshot = fixture();
  snapshot.version = 19;
  snapshot.nodes[0].prototypeOverflow = 'vertical';
  snapshot.nodes.find((n) => n.id === id(20)).prototypeOverflow = 'both';
  snapshot.nodes[1].interaction = { trigger: 'click', action: 'open-overlay', target: id(20) };
  await using(snapshot, async (document) => {
    const detail = await (await fetch(new URL(`/api/documents/${document}`, origin))).json();
    assert.equal(detail.snapshot.data.version, 19);
    assert.equal(detail.snapshot.data.nodes.find((n) => n.id === id(10)).prototypeOverflow, 'vertical');
    assert.equal(detail.snapshot.data.nodes.find((n) => n.id === id(20)).prototypeOverflow, 'both');
    for (const prototypeOverflow of ['scroll', '', null, true, 1, {}, ['vertical']]) {
      refuse(await append(document, 'SetProps', { id: id(10), patch: { prototypeOverflow } }));
    }
    for (const target of [id(11), id(30), id(32)]) {
      refuse(await append(document, 'SetProps', { id: target, patch: { prototypeOverflow: 'none' } }));
    }
    refuse(await append(document, 'SetOverride', {
      id: id(32), node: id(31), patch: { prototypeOverflow: 'vertical' },
    }));
    for (const prototypeOverflow of ['horizontal', 'both', 'none']) {
      accept(await append(document, 'SetProps', { id: id(10), patch: { prototypeOverflow } }));
    }
    const reloaded = await (await fetch(new URL(`/api/documents/${document}`, origin))).json();
    assert.equal(reloaded.snapshot.data.version, 19);
    const ops = await (await fetch(new URL(`/api/documents/${document}/ops?after=1`, origin))).json();
    assert.equal(ops.ops.length, 3, 'refused overflow changes must not enter the persisted oplog');
    assert.deepEqual(ops.ops.map((op) => op.data.patch), [
      { prototypeOverflow: 'horizontal' },
      { prototypeOverflow: 'both' },
      { prototypeOverflow: 'none' },
    ]);
    assert.ok(ops.ops.every((op) => op.kind === 'SetProps' && op.data.id === id(10)));
  });
  const downlevel = structuredClone(snapshot);
  downlevel.version = 18;
  const invalidSnapshots = [downlevel];
  for (const target of [id(11), id(30), id(32)]) {
    const misplaced = structuredClone(snapshot);
    misplaced.nodes.find((n) => n.id === target).prototypeOverflow = 'none';
    invalidSnapshots.push(misplaced);
  }
  const badValue = structuredClone(snapshot);
  badValue.nodes[0].prototypeOverflow = 'scroll';
  invalidSnapshots.push(badValue);
  for (const invalid of invalidSnapshots) {
    const response = await request('/api/documents/import', { name: 'Invalid overflow QA', snapshot: invalid });
    assert.equal(response.status, 400, await response.text());
  }
});

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
