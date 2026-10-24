import assert from 'node:assert/strict';
import { test } from 'node:test';
import { readComponent } from '../client/src/app/editor/component-package.ts';
const id = (n) => `00000000-0000-0000-0000-${String(n).padStart(12, '0')}`;
const fixture = () => ({
  format: 'canvas-component/1',
  name: 'Path',
  componentId: id(1),
  snapshot: {
    version: 13,
    modes: [],
    tokens: [],
    nodes: [
      { id: id(1), type: 'component', order: 'a0', name: 'Path', w: 200, h: 100 },
      {
        id: id(2),
        type: 'path',
        parent: id(1),
        order: 'a0',
        x: 10,
        y: 20,
        w: 100,
        h: 80,
        lineWidth: 4,
        path: {
          points: [
            { x: 0, y: 0 },
            { x: 1, y: 1 },
            { x: 0, y: 1 },
          ],
          closed: true,
        },
      },
    ],
  },
});
test('native paths preserve vertices and closure in snapshot 13 and refuse downgrade', () => {
  const data = fixture(),
    parsed = readComponent(JSON.stringify(data));
  assert.deepEqual(
    parsed.snapshot.nodes.find((node) => node.type === 'path').path,
    data.snapshot.nodes[1].path,
  );
  data.snapshot.version = 12;
  assert.throws(() => readComponent(JSON.stringify(data)), /version 13/);
});
test('portable path geometry refuses invalid counts, unknown fields, ranges and path-only fields', () => {
  for (const geometry of [
    {
      points: [
        [0, 0],
        [1, 1],
      ],
      closed: false,
    },
    { points: [{ x: 0, y: 0 }], closed: false },
    {
      points: [
        { x: 0, y: 0 },
        { x: 1, y: 1 },
      ],
      closed: true,
    },
    {
      points: [
        { x: 0, y: 0 },
        { x: 2, y: 1 },
      ],
      closed: false,
    },
    {
      points: [
        { x: 0, y: 0 },
        { x: 1, y: 1 },
      ],
      closed: false,
      handles: [],
    },
    { points: Array.from({ length: 65 }, () => ({ x: 0, y: 0 })), closed: false },
  ]) {
    const data = fixture();
    data.snapshot.nodes[1].path = geometry;
    assert.throws(() => readComponent(JSON.stringify(data)), /Path geometry/);
  }
  const data = fixture();
  data.snapshot.nodes[0].path = data.snapshot.nodes[1].path;
  assert.throws(() => readComponent(JSON.stringify(data)), /Path geometry/);
});

test('path overrides retain native geometry and refuse wrong targets and malformed values', () => {
  const data = fixture(),
    component = data.snapshot.nodes[0];
  component.definition = {
    axes: [{ name: 'shape', values: ['open', 'closed'] }],
    rules: [
      {
        target: id(2),
        when: { shape: 'open' },
        patch: {
          path: {
            points: [
              { x: 0, y: 0 },
              { x: 1, y: 0 },
            ],
            closed: false,
          },
        },
      },
    ],
  };
  const parsed = readComponent(JSON.stringify(data));
  assert.deepEqual(
    parsed.snapshot.nodes[0].definition.rules[0].patch.path,
    component.definition.rules[0].patch.path,
  );
  component.definition.rules[0].target = id(1);
  assert.throws(() => readComponent(JSON.stringify(data)), /must target native path/);
  component.definition.rules[0].target = id(2);
  component.definition.rules[0].patch.path.points[0].x = -0.1;
  assert.throws(() => readComponent(JSON.stringify(data)), /Invalid path override/);
});

test('curved and filled paths require version 14 and preserve controls faithfully', () => {
  const data = fixture(),
    path = data.snapshot.nodes[1].path;
  path.inHandles = [
    { x: 0, y: 0 },
    { x: 0.75, y: 1 },
    { x: 0, y: 1 },
  ];
  path.outHandles = [
    { x: 0.25, y: 0 },
    { x: 1, y: 1 },
    { x: 0, y: 1 },
  ];
  assert.throws(() => readComponent(JSON.stringify(data)), /version 14/);
  data.snapshot.version = 14;
  path.filled = true;
  assert.deepEqual(readComponent(JSON.stringify(data)).snapshot.nodes[1].path, path);
  delete path.inHandles;
  delete path.outHandles;
  data.snapshot.version = 13;
  assert.throws(() => readComponent(JSON.stringify(data)), /version 14/);
  path.filled = false;
  assert.equal(readComponent(JSON.stringify(data)).snapshot.version, 13);
});
test('curve controls require matched bounded arrays and filled paths require closure', () => {
  for (const fields of [
    { inHandles: [{ x: 0, y: 0 }], outHandles: [] },
    {
      inHandles: [
        { x: 0, y: 0 },
        { x: 0, y: 0 },
        { x: 0, y: 0 },
      ],
      outHandles: [
        { x: 0, y: 0 },
        { x: 0, y: 0 },
        { x: 1.01, y: 0 },
      ],
    },
    { filled: 'yes' },
    { filled: true, closed: false },
  ]) {
    const data = fixture();
    data.snapshot.version = 14;
    Object.assign(data.snapshot.nodes[1].path, fields);
    assert.throws(() => readComponent(JSON.stringify(data)), /Path geometry/);
  }
});
