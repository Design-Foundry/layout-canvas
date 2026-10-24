import assert from 'node:assert/strict';
import { test } from 'node:test';
import {
  exportComponent,
  readComponent,
  MAX_COMPONENT_BYTES,
} from '../client/src/app/editor/component-package.ts';

const id = (n) => `00000000-0000-0000-0000-${String(n).padStart(12, '0')}`;

const component = (n, more = {}) => ({
  id: id(n),
  type: 'component',
  name: `Component ${n}`,
  order: 'a0',
  w: 120,
  h: 40,
  ...more,
});

const fixture = () => ({
  version: 2,
  modes: [
    { id: id(1), name: 'Default', order: 'a0' },
    { id: id(2), name: 'Dark', order: 'a1' },
  ],
  tokens: [
    {
      id: id(100),
      identifier: 'accent',
      type: 'color',
      order: 'a0',
      value: { l: 0.6, c: 0.2, h: 120 },
    },
    {
      id: id(101),
      identifier: 'action',
      type: 'color',
      order: 'a1',
      source: { [id(1)]: id(100), [id(2)]: id(100) },
    },
    {
      id: id(102),
      identifier: 'space',
      type: 'dimension',
      order: 'a0',
      value: { value: 8, unit: 'px' },
    },
  ],
  nodes: [
    component(10, { x: 500, y: 300 }),
    {
      id: id(11),
      type: 'rect',
      parent: id(10),
      order: 'a0',
      name: 'Icon',
      x: 8,
      y: 8,
      w: 24,
      h: 24,
      fill: { token: id(101) },
    },
    component(20),
  ],
});

function variantFixture() {
  const document = fixture();
  document.version = 3;
  document.nodes[0].definition = {
    properties: [{ name: 'show-icon', target: id(11), default: false }],
    axes: [
      { name: 'size', values: ['compact', 'large'], default: 'compact' },
      { name: 'tone', values: ['neutral', 'danger'], default: 'neutral' },
    ],
    rules: [
      { when: { size: 'large', tone: 'danger' }, target: id(11), patch: { cornerRadius: 12 } },
      {
        when: { size: 'large' },
        target: id(11),
        patch: { w: 32, fill: { token: id(101) }, fillVisible: true },
      },
    ],
  };
  document.nodes.push({
    id: id(21),
    type: 'instance',
    parent: id(20),
    component: id(10),
    order: 'a0',
    componentValues: { 'show-icon': true, size: 'large', tone: 'danger' },
  });
  return document;
}

function textExchangeFixture() {
  const source = fixture();
  source.version = 6;
  source.nodes[0].definition = {
    properties: [],
    axes: [],
    rules: [],
    texts: [{ name: 'label', target: id(910), default: 'Continuar' }],
  };
  source.nodes.push({
    id: id(910),
    parent: id(10),
    type: 'text',
    order: 'a1',
    widthSizing: 'hug',
    heightSizing: 'hug',
    text: {
      content: 'Continuar',
      font: 'inter-regular-v1',
      fontSize: 28,
      lineHeight: 1.6,
      letterSpacing: 2,
      align: 'right',
    },
    fill: { token: id(101) },
  });
  source.nodes.push({
    id: id(911),
    parent: id(20),
    component: id(10),
    type: 'instance',
    order: 'a0',
    w: 220,
    h: 60,
    componentValues: { label: 'Guardar diseño\nAhora' },
  });
  return exportComponent(source, id(20));
}

test('portable export isolates the component, preserves tokens and does not mutate the document', () => {
  const document = fixture();
  const original = structuredClone(document);
  const bundle = exportComponent(document, id(10));
  assert.deepEqual(
    bundle.snapshot.nodes.map((node) => node.id),
    [id(10), id(11)],
  );
  assert.equal(bundle.snapshot.nodes[0].x, 0);
  assert.deepEqual(bundle.snapshot.tokens, original.tokens);
  assert.deepEqual(document, original);
  assert.deepEqual(readComponent(JSON.stringify(bundle)), bundle);
});

test('portable export includes main components referenced by nested instances', () => {
  const document = fixture();
  document.nodes.push({
    id: id(12),
    type: 'instance',
    parent: id(10),
    component: id(20),
    order: 'a1',
  });
  const bundle = readComponent(JSON.stringify(exportComponent(document, id(10))));
  assert.equal(bundle.snapshot.nodes.find((node) => node.id === id(20)).type, 'component');
});

test('swap properties preserve every preferred dependency in snapshot 4', () => {
  const source = fixture();
  source.version = 4;
  source.nodes.push({
    id: id(30),
    type: 'component',
    name: 'Unused preferred icon',
    order: 'a3',
    w: 24,
    h: 24,
  });
  source.nodes.push({
    id: id(12),
    type: 'instance',
    parent: id(10),
    component: id(20),
    order: 'a1',
  });
  source.nodes[0].definition = {
    properties: [],
    axes: [],
    rules: [],
    swaps: [{ name: 'icon', target: id(12), default: id(20), choices: [id(20), id(30)] }],
  };
  const bundle = exportComponent(source, id(10));
  assert.deepEqual(readComponent(JSON.stringify(bundle)), bundle);
  assert.equal(
    bundle.snapshot.nodes.some((n) => n.id === id(30)),
    true,
  );
  assert.equal(bundle.snapshot.version, 4);
  source.version = 3;
  assert.throws(() => exportComponent(source, id(10)), /version 4/);
  source.version = 4;
  source.nodes[0].definition.swaps[0].choices.push(id(10));
  assert.throws(() => exportComponent(source, id(10)), /recursive/);
  source.nodes[0].definition.swaps[0].choices = [id(999)];
  assert.throws(() => exportComponent(source, id(10)), /invalid targets or missing/);
});

test('version 3 portable files preserve boolean properties, multi-axis rules and composed selections', () => {
  const document = variantFixture();
  const portable = exportComponent(document, id(20));
  assert.deepEqual(readComponent(JSON.stringify(portable)), portable);
  assert.equal(portable.snapshot.version, 3);
  document.version = 2;
  assert.throws(() => exportComponent(document, id(20)), /version 3/);
});

test('portable import rejects invalid references, duplicate ids and cyclic graphs', () => {
  for (const mutate of [
    (document) => {
      document.nodes[1].parent = id(999);
    },
    (document) => {
      document.nodes.push({ ...document.nodes[0] });
    },
    (document) => {
      document.nodes[0].parent = id(11);
    },
    (document) => {
      document.nodes.push({
        id: id(12),
        type: 'instance',
        parent: id(10),
        component: id(10),
        order: 'a1',
      });
    },
  ]) {
    const document = fixture();
    mutate(document);
    assert.throws(() => exportComponent(document, id(10)));
  }
});

test('portable import refuses malformed, unsupported and oversized files', () => {
  assert.throws(() => readComponent('{'));
  assert.throws(() => readComponent(JSON.stringify({ format: 'canvas-component/99' })));
  assert.throws(() => readComponent(' '.repeat(MAX_COMPONENT_BYTES + 1)), /2 MB/);
  assert.throws(() => exportComponent(fixture(), id(11)), /main component/);
});

test('snapshot 5 portable components keep native text and refuse a downgrade', () => {
  const source = fixture();
  source.version = 5;
  const root = source.nodes.find((node) => node.type === 'component');
  const text = {
    content: 'Diseño: José\nContinuar',
    font: 'inter-regular-v1',
    fontSize: 24,
    lineHeight: 1.4,
    letterSpacing: 0,
    align: 'center',
  };
  source.nodes.push({
    id: id(902),
    type: 'text',
    parent: root.id,
    order: 'z0',
    name: 'Label',
    text,
    widthSizing: 'hug',
    heightSizing: 'hug',
  });
  const bundle = exportComponent(source, root.id);
  assert.deepEqual(
    readComponent(JSON.stringify(bundle)).snapshot.nodes.find((node) => node.id === id(902)).text,
    text,
  );
  bundle.snapshot.version = 4;
  assert.throws(() => readComponent(JSON.stringify(bundle)), /snapshot version 5/);
});

test('snapshot 6 preserves declared text defaults and refuses mislabeling as version 5', () => {
  const source = fixture();
  source.version = 6;
  const root = source.nodes.find((node) => node.type === 'component');
  root.definition = {
    properties: [],
    axes: [],
    rules: [],
    texts: [{ name: 'label', target: id(903), default: 'A'.repeat(200) }],
  };
  source.nodes.push({
    id: id(903),
    parent: root.id,
    order: 'z0',
    type: 'text',
    name: 'Label',
    text: {
      content: 'base',
      font: 'inter-regular-v1',
      fontSize: 24,
      lineHeight: 1.4,
      letterSpacing: 0,
      align: 'left',
    },
  });
  const bundle = readComponent(JSON.stringify(exportComponent(source, root.id)));
  assert.equal(
    bundle.snapshot.nodes.find((node) => node.id === root.id).definition.texts[0].default,
    'A'.repeat(200),
  );
  bundle.snapshot.version = 5;
  assert.throws(() => readComponent(JSON.stringify(bundle)), /snapshot version 6/);
});

test('partial text variant fields round-trip independently and cannot be downgraded', () => {
  const source = textExchangeFixture();
  source.snapshot.version = 7;
  const main = source.snapshot.nodes.find((n) => n.definition?.texts?.length);
  main.definition.axes = [
    { name: 'size', values: ['small', 'large'], default: 'small' },
    { name: 'tone', values: ['normal', 'wide'], default: 'normal' },
  ];
  const target = main.definition.texts[0].target;
  main.definition.rules = [
    { target, when: { size: 'large' }, patch: { textStyle: { fontSize: 32 } } },
    { target, when: { tone: 'wide' }, patch: { textStyle: { lineHeight: 2, align: 'center' } } },
  ];
  source.snapshot.nodes.find((n) => n.type === 'instance').componentValues = {
    label: 'Guardar diseño',
    size: 'large',
    tone: 'wide',
  };
  const imported = readComponent(JSON.stringify(source));
  assert.equal(imported.snapshot.version, 7);
  const rules = imported.snapshot.nodes.find((n) => n.definition?.texts?.length).definition.rules;
  assert.deepEqual(
    rules.map((r) => r.patch),
    main.definition.rules.map((r) => r.patch),
  );
  imported.snapshot.version = 6;
  assert.throws(() => readComponent(JSON.stringify(imported)), /snapshot version 7/);
});

test('removing one conditional text field leaves it absent in a portable package', () => {
  const source = textExchangeFixture();
  source.snapshot.version = 7;
  const main = source.snapshot.nodes.find((n) => n.definition?.texts?.length);
  main.definition.axes = [{ name: 'tone', values: ['normal', 'wide'], default: 'normal' }];
  const target = main.definition.texts[0].target;
  main.definition.rules = [
    {
      target,
      when: { tone: 'wide' },
      patch: { textStyle: { fontSize: 32, lineHeight: 2, letterSpacing: 3, align: 'center' } },
    },
  ];
  delete main.definition.rules[0].patch.textStyle.lineHeight;
  const returned = readComponent(JSON.stringify(source));
  const restored = returned.snapshot.nodes.find((n) => n.definition?.texts?.length).definition;
  assert.deepEqual(restored.rules[0].patch.textStyle, {
    fontSize: 32,
    letterSpacing: 3,
    align: 'center',
  });
  main.definition.rules = [];
  const empty = readComponent(JSON.stringify(source));
  assert.deepEqual(
    empty.snapshot.nodes.find((n) => n.definition?.texts?.length).definition.rules,
    [],
  );
});

test('wrapped text retains snapshot 8 in native files and cannot be downgraded', () => {
  const source = textExchangeFixture();
  source.snapshot.version = 8;
  const text = source.snapshot.nodes.find((node) => node.type === 'text');
  text.textWrap = true;
  const portable = readComponent(JSON.stringify(source));
  assert.equal(portable.snapshot.nodes.find((node) => node.type === 'text').textWrap, true);
  source.snapshot.version = 7;
  assert.throws(() => readComponent(JSON.stringify(source)), /version 8/);
  text.textWrap = false;
  const instance = source.snapshot.nodes.find((node) => node.type === 'instance');
  instance.overrides = [{ node: text.id, patch: { textWrap: false } }];
  assert.throws(() => readComponent(JSON.stringify(source)), /version 8/);
});

test('trusted font selections persist in snapshot 9 and refuse older files', () => {
  const source = textExchangeFixture();
  source.snapshot.version = 9;
  const text = source.snapshot.nodes.find((node) => node.type === 'text');
  text.text.font = 'inter-bold-v1';
  assert.equal(
    readComponent(JSON.stringify(source)).snapshot.nodes.find((node) => node.type === 'text').text
      .font,
    'inter-bold-v1',
  );
  source.snapshot.version = 8;
  assert.throws(() => readComponent(JSON.stringify(source)), /version 9/);
  text.text.font = 'inter-regular-v1';
  const instance = source.snapshot.nodes.find((node) => node.type === 'instance');
  instance.overrides = [{ node: text.id, patch: { textStyle: { font: 'inter-regular-v1' } } }];
  assert.throws(() => readComponent(JSON.stringify(source)), /version 9/);
});

test('oblique faces require snapshot 10 in source nodes, dormant rules and overrides', () => {
  for (const font of ['inter-oblique-v1', 'inter-bold-oblique-v1']) {
    const source = textExchangeFixture();
    source.snapshot.version = 10;
    const text = source.snapshot.nodes.find((node) => node.type === 'text');
    text.text.font = font;
    assert.equal(readComponent(JSON.stringify(source)).snapshot.version, 10);
    source.snapshot.version = 9;
    assert.throws(() => readComponent(JSON.stringify(source)), /version 10/);
    text.text.font = 'inter-regular-v1';
    const instance = source.snapshot.nodes.find((node) => node.type === 'instance');
    instance.overrides = [{ node: text.id, patch: { textStyle: { font } } }];
    assert.throws(() => readComponent(JSON.stringify(source)), /version 10/);
    instance.overrides = [];
    const main = source.snapshot.nodes.find((node) => node.type === 'component');
    main.definition.rules = [
      { target: text.id, when: { size: 'large' }, patch: { textStyle: { font } } },
    ];
    assert.throws(() => readComponent(JSON.stringify(source)), /version 10/);
  }
});

test('shared typography preserves the editable library and source reference in snapshot 11', () => {
  const source = textExchangeFixture();
  source.snapshot.version = 11;
  const text = source.snapshot.nodes.find((node) => node.type === 'text');
  const token = {
    id: '00000000-0000-0000-0000-000000000979',
    type: 'typography',
    identifier: 'type.body',
    order: 'a0',
    value: {
      font: 'inter-regular-v1',
      fontSize: 24,
      lineHeight: 1.4,
      letterSpacing: 0,
      align: 'left',
    },
  };
  source.snapshot.tokens.push(token);
  text.typography = token.id;
  const parsed = readComponent(JSON.stringify(source));
  assert.deepEqual(parsed.snapshot.tokens.at(-1), token);
  assert.equal(parsed.snapshot.nodes.find((node) => node.type === 'text').typography, token.id);
  source.snapshot.version = 10;
  assert.throws(() => readComponent(JSON.stringify(source)), /version 11/);
  source.snapshot.version = 11;
  text.typography = '00000000-0000-0000-0000-000000000978';
  assert.throws(() => readComponent(JSON.stringify(source)), /missing or incompatible typography/);
  delete text.typography;
  source.snapshot.version = 10;
  assert.throws(() => readComponent(JSON.stringify(source)), /version 11/);
});

test('native lines preserve direction, zero-axis dimensions and width in snapshot 12', () => {
  const source = textExchangeFixture();
  source.snapshot.version = 12;
  const text = source.snapshot.nodes.find((node) => node.type === 'text');
  text.type = 'line';
  delete text.text;
  text.lineWidth = 6;
  text.lineReverse = true;
  text.w = 180;
  text.h = 0;
  const main = source.snapshot.nodes.find((node) => node.type === 'component');
  main.definition.texts = [];
  const parsed = readComponent(JSON.stringify(source));
  const line = parsed.snapshot.nodes.find((node) => node.type === 'line');
  assert.equal(line.lineWidth, 6);
  assert.equal(line.lineReverse, true);
  assert.equal(line.h, 0);
  source.snapshot.version = 11;
  assert.throws(() => readComponent(JSON.stringify(source)), /version 12/);
  source.snapshot.version = 12;
  text.lineWidth = -1;
  assert.throws(() => readComponent(JSON.stringify(source)), /invalid width or direction/);
  text.lineWidth = 4;
  text.lineReverse = 'up';
  assert.throws(() => readComponent(JSON.stringify(source)), /invalid width or direction/);
  text.lineReverse = false;
  main.lineWidth = 4;
  assert.throws(() => readComponent(JSON.stringify(source)), /Line-only fields/);
});
