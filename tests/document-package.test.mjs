import assert from 'node:assert/strict';
import { test } from 'node:test';
import {
  documentFilename, exportDocument, readDocument,
  MAX_DOCUMENT_FILE_BYTES, MAX_DOCUMENT_SNAPSHOT_BYTES,
} from '../client/src/app/editor/document-package.ts';

const id = (n) => `00000000-0000-0000-0000-${String(n).padStart(12, '0')}`;
const fixture = () => ({
  version: 22,
  modes: [{ id: id(1), name: 'Default', order: 'a0' }],
  tokens: [],
  nodes: [
    { id: id(2), type: 'frame', order: 'a0', name: 'Home', w: 400, h: 300,
      prototypeStart: true, prototypeFlowName: 'Checkout', prototypeOverflow: 'vertical' },
    { id: id(3), type: 'rect', parent: id(2), order: 'a0',
      interactions: [{ trigger: 'click', action: 'navigate', target: id(4), transition: 'smart-animate', durationMs: 300 }] },
    { id: id(4), type: 'frame', order: 'a1', name: 'Detail', w: 600, h: 400 },
    { id: id(5), type: 'image', parent: id(4), order: 'a0',
      image: { id: 'a'.repeat(64), width: 1, height: 1, pixels: '/wAA/w==' } },
    { id: id(6), type: 'component', order: 'a2',
      definition: { axes: [{ name: 'state', values: ['off', 'on'], default: 'off' }], rules: [] },
      interaction: { trigger: 'click', action: 'change-state', values: { state: 'on' } } },
    { id: id(7), type: 'instance', component: id(6), parent: id(2), order: 'a1',
      componentValues: { state: 'off' }, overrides: [] },
  ],
});

test('whole-document round trip preserves every screen, relative state action, flow and inline image', () => {
  const snapshot = fixture();
  const bundle = readDocument(JSON.stringify(exportDocument(snapshot, '  Checkout demo  ')));
  assert.equal(bundle.name, 'Checkout demo');
  assert.deepEqual(bundle.snapshot, snapshot);
  assert.equal(bundle.snapshot.nodes[1].interactions[0].target, id(4));
});

test('export captures a detached snapshot instead of retaining mutable authoring references', () => {
  const snapshot = fixture();
  const bundle = exportDocument(snapshot, 'Demo');
  snapshot.nodes[0].name = 'Changed';
  assert.equal(bundle.snapshot.nodes[0].name, 'Home');
});

test('document envelope refuses components, unknown formats and extra metadata', () => {
  const valid = exportDocument(fixture(), 'Demo');
  for (const changed of [
    { ...valid, format: 'canvas-component/1' },
    { ...valid, format: 'canvas-document/2' },
    { ...valid, host: 'https://example.com' },
    { ...valid, snapshot: undefined },
  ]) assert.throws(() => readDocument(JSON.stringify(changed)), /complete Canvas document/);
});

test('native snapshot envelope refuses unsupported versions and missing structural lists', () => {
  for (const snapshot of [
    { ...fixture(), version: 23 },
    { ...fixture(), version: 0 },
    { ...fixture(), version: 21.5 },
    { ...fixture(), nodes: [null] },
    { ...fixture(), tokens: undefined },
    { ...fixture(), modes: {} },
    { ...fixture(), nodes: new Array(10001).fill({}) },
    { ...fixture(), camera: { x: 100 } },
  ]) assert.throws(() => exportDocument(snapshot, 'Demo'), /supported Canvas document snapshot/);
});

test('legacy native version 1 can reopen without inventing token or mode lists', () => {
  const snapshot = { version: 1, nodes: [] };
  assert.deepEqual(readDocument(JSON.stringify(exportDocument(snapshot, 'Legacy'))).snapshot, snapshot);
});

test('size limits measure UTF8 and count inline document payload before wrapping it', () => {
  const snapshot = fixture();
  snapshot.nodes[0].name = 'é'.repeat(MAX_DOCUMENT_SNAPSHOT_BYTES / 2);
  assert.throws(() => exportDocument(snapshot, 'Demo'), /exceeds the 2 MB/);
  assert.throws(() => readDocument(' '.repeat(MAX_DOCUMENT_FILE_BYTES + 1)), /exceeds the 2 MB/);
});

test('invalid JSON and unserializable snapshots produce actionable errors', () => {
  assert.throws(() => readDocument('{'), /not valid JSON/);
  const circular = fixture();
  circular.nodes[0].self = circular;
  assert.throws(() => exportDocument(circular, 'Demo'), /could not be serialized/);
});

test('document names are bounded and generated filenames remove platform path characters', () => {
  for (const name of ['', '  ', 'x'.repeat(201)])
    assert.throws(() => exportDocument(fixture(), name), /between 1 and 200/);
  assert.equal(documentFilename('../Demo: one/two?'), '..-Demo- one-two-.canvas.json');
  assert.equal(documentFilename('...'), 'canvas-document.canvas.json');
  assert.equal(documentFilename('x'.repeat(150)), `${'x'.repeat(100)}.canvas.json`);
});
