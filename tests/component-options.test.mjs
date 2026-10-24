import assert from 'node:assert/strict';
import { test } from 'node:test';
import { componentOptions } from '../client/src/app/editor/component-options.ts';

test('swap choices exclude both the enclosing main and transitive composition cycles', () => {
  const nodes = [
    { id: 'a', type: 'component', name: 'A' },
    { id: 'b', type: 'component', name: 'B' },
    { id: 'c', type: 'component', name: 'C' },
    { id: 'd', type: 'component', name: 'D' },
    { id: 'target', type: 'instance', parent: 'a', component: 'b' },
    { id: 'c-uses-a', type: 'instance', parent: 'c', component: 'a' },
    { id: 'd-uses-c', type: 'instance', parent: 'd', component: 'c' },
  ];
  assert.deepEqual(componentOptions({ nodes }, 'target'), [{ label: 'B', value: 'b' }]);
  nodes.push({ id: 'top', type: 'instance', component: 'a' });
  assert.equal(componentOptions({ nodes }, 'top').length, 4);
});

test('swap choices find the main through nested frames and do not mutate the snapshot', () => {
  const snapshot = {
    nodes: [
      { id: 'a', type: 'component', name: 'Host' },
      { id: 'frame', type: 'frame', parent: 'a' },
      { id: 'target', type: 'instance', parent: 'frame', component: 'b' },
      { id: 'b', type: 'component', name: 'Button' },
      { id: 'c', type: 'component', name: 'Alert' },
    ],
  };
  const original = structuredClone(snapshot);
  assert.deepEqual(componentOptions(snapshot, 'target'), [
    { label: 'Alert', value: 'c' },
    { label: 'Button', value: 'b' },
  ]);
  assert.deepEqual(snapshot, original);
});

test('cycle filtering considers unselected declared swap choices', () => {
  const nodes = [
    { id: 'a', type: 'component', name: 'A' },
    { id: 'b', type: 'component', name: 'B', definition: { swaps: [{ choices: ['a'] }] } },
    { id: 'c', type: 'component', name: 'C' },
    { id: 'target', type: 'instance', parent: 'a', component: 'c' },
  ];
  assert.deepEqual(componentOptions({ nodes }, 'target'), [{ label: 'C', value: 'c' }]);
});
