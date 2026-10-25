import assert from 'node:assert/strict';
import { test } from 'node:test';
import { readComponent, exportComponent } from '../client/src/app/editor/component-package.ts';
const id = (n) => `00000000-0000-0000-0000-${String(n).padStart(12, '0')}`;
const fixture = () => ({
  format: 'canvas-component/1',
  name: 'Button',
  componentId: id(1),
  snapshot: {
    version: 16,
    nodes: [{ id: id(1), type: 'component', order: 'a0', w: 100, h: 40 }],
    tokens: [],
    modes: [],
  },
});
test('snapshot20 without ordered prototype metadata remains portable', () => {
  const data = fixture();
  data.snapshot.version = 20;
  assert.equal(readComponent(JSON.stringify(data)).snapshot.version, 20);
  assert.equal(exportComponent(data.snapshot, id(1)).snapshot.version, 20);
});
test('ordered prototype lists including empty masks cannot escape in portable components', () => {
  for (const interactions of [[], [{ trigger: 'hover', action: 'back' }], [{ trigger: 'delay', delayMs: 500, action: 'back' }]]) {
    for (const location of ['node', 'override', 'rule']) {
      const data = fixture();
      data.snapshot.version = 20;
      if (location === 'node') data.snapshot.nodes[0].interactions = interactions;
      if (location === 'override') data.snapshot.nodes[0].overrides = [{ node: id(2), patch: { interactions } }];
      if (location === 'rule') data.snapshot.nodes[0].definition = { rules: [{ target: id(2), when: { state: 'a' }, patch: { interactions } }] };
      assert.throws(() => readComponent(JSON.stringify(data)), /document-local/);
      assert.throws(() => exportComponent(data.snapshot, id(1)), /document-local/);
    }
  }
});
test('native component files refuse document-local prototype metadata including explicit null and false', () => {
  for (const fields of [
    { interaction: null },
    { prototypeStart: false },
    { interaction: { trigger: 'click', action: 'back' } },
    { interaction: { trigger: 'click', action: 'navigate', target: id(3) } },
    {
      interaction: {
        trigger: 'click',
        action: 'open-overlay',
        target: id(3),
        transition: 'dissolve',
        durationMs: 450,
        dismissOutside: false,
      },
    },
    { interaction: { trigger: 'click', action: 'close-overlay' } },
  ]) {
    const data = fixture();
    if (
      fields.interaction?.action === 'open-overlay' ||
      fields.interaction?.action === 'close-overlay'
    )
      data.snapshot.version = 17;
    Object.assign(data.snapshot.nodes[0], fields);
    assert.throws(() => readComponent(JSON.stringify(data)), /document-local/);
    assert.throws(() => exportComponent(data.snapshot, id(1)), /document-local/);
  }
});
test('prototype metadata in overrides or variant rules is refused before portable conversion', () => {
  for (const key of ['overrides', 'rules']) {
    const data = fixture(),
      patch = { interaction: { trigger: 'click', action: 'back' } };
    if (key === 'overrides') data.snapshot.nodes[0].overrides = [{ node: id(2), patch }];
    else
      data.snapshot.nodes[0].definition = {
        rules: [{ target: id(2), when: { tone: 'a' }, patch }],
      };
    assert.throws(() => readComponent(JSON.stringify(data)), /document-local/);
  }
});
test('snapshot16 without authored prototype metadata remains portable', () => {
  const data = fixture();
  assert.equal(readComponent(JSON.stringify(data)).snapshot.version, 16);
});
test('snapshot17 without document-local metadata remains portable', () => {
  const data = fixture();
  data.snapshot.version = 17;
  assert.equal(readComponent(JSON.stringify(data)).snapshot.version, 17);
});
test('snapshot18 preserves portable data without document-local prototype metadata', () => {
  const data = fixture();
  data.snapshot.version = 18;
  assert.equal(readComponent(JSON.stringify(data)).snapshot.version, 18);
});
test('snapshot18 positioned overlays and directional transitions refuse component export', () => {
  for (const interaction of [
    { trigger: 'click', action: 'navigate', target: id(3), transition: 'push-up', durationMs: 500 },
    {
      trigger: 'click',
      action: 'open-overlay',
      target: id(3),
      transition: 'slide-right',
      durationMs: 650,
      placement: 'bottom-left',
    },
    {
      trigger: 'click',
      action: 'open-overlay',
      target: id(3),
      placement: 'manual',
      offset: { x: -40.5, y: 90.25 },
    },
  ]) {
    const data = fixture();
    data.snapshot.version = 18;
    data.snapshot.nodes[0].interaction = interaction;
    assert.throws(() => readComponent(JSON.stringify(data)), /document-local/);
    assert.throws(() => exportComponent(data.snapshot, id(1)), /document-local/);
  }
});

test('snapshot19 without document-local overflow remains portable', () => {
  const data = fixture();
  data.snapshot.version = 19;
  assert.equal(readComponent(JSON.stringify(data)).snapshot.version, 19);
});
test('prototype overflow refuses portable files on nodes and nested patches including none', () => {
  for (const value of ['none', 'vertical', 'horizontal', 'both']) {
    for (const location of ['node', 'overrides', 'rules']) {
      const data = fixture();
      data.snapshot.version = 19;
      const patch = { prototypeOverflow: value };
      if (location === 'node') Object.assign(data.snapshot.nodes[0], patch);
      else if (location === 'overrides') data.snapshot.nodes[0].overrides = [{ node: id(2), patch }];
      else data.snapshot.nodes[0].definition = { rules: [{ target: id(2), when: { tone: 'a' }, patch }] };
      assert.throws(() => readComponent(JSON.stringify(data)), /document-local/);
      assert.throws(() => exportComponent(data.snapshot, id(1)), /document-local/);
    }
  }
});
