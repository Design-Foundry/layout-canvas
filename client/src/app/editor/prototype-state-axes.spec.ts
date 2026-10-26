import type { NodeProps, VariantAxis } from '@canvas/bridge';
import { findPrototypeStateAxes } from './prototype-state-axes';

const axes: VariantAxis[] = [{ name: 'State', values: ['Idle', 'Active'], default: 'Idle' }];
const node = (value: Partial<NodeProps>) => value as NodeProps;
describe('prototype component axis context', () => {
  it('uses the effective nested component rather than the outer component or original swap target', () => {
    const nestedAxes: VariantAxis[] = [{ name: 'size', values: ['small', 'large'], default: 'small' }];
    const nodes = new Map([
      ['copy', node({ id: 'copy', type: 'instance', component: 'swapped-main', parent: 'outer', inInstance: true })],
      ['swapped-main', node({ id: 'swapped-main', type: 'component', definition: { properties: [], axes: nestedAxes, rules: [] } })],
      ['outer', node({ id: 'outer', type: 'instance', definition: { properties: [], axes, rules: [] } })],
    ]);
    expect(findPrototypeStateAxes(node({ id: 'hotspot', type: 'rect', parent: 'copy', inInstance: true }), (id) => nodes.get(id) ?? null)).toBe(nestedAxes);
  });
  it('finds the source component when authoring a reusable child hotspot', () => {
    const main = node({ id: 'main', type: 'component', definition: { properties: [], axes, rules: [] } });
    expect(findPrototypeStateAxes(node({ id: 'child', type: 'rect', parent: 'main' }), (id) => id === 'main' ? main : null)).toBe(axes);
  });
  it('resolves copied-layer ancestry through the authored instance and its main component', () => {
    const nodes = new Map([
      ['instance', node({ id: 'instance', type: 'instance', component: 'main' })],
      ['main', node({ id: 'main', type: 'component', definition: { properties: [], axes, rules: [] } })],
    ]);
    expect(findPrototypeStateAxes(node({ id: 'copy', type: 'rect', parent: 'instance', inInstance: true }), (id) => nodes.get(id) ?? null)).toBe(axes);
  });
  it('does not fall back to a different outer instance when the nearest component has no axes', () => {
    const outer = node({ id: 'outer', type: 'instance', definition: { properties: [], axes, rules: [] } });
    expect(findPrototypeStateAxes(node({ id: 'nested', type: 'instance', inInstance: true, parent: 'outer', definition: { properties: [], axes: [], rules: [] } }), () => outer)).toEqual([]);
  });
  it('terminates on unknown parents and malformed cycles without suggesting unrelated values', () => {
    const a = node({ id: 'a', type: 'frame', parent: 'b' });
    const b = node({ id: 'b', type: 'frame', parent: 'a' });
    expect(findPrototypeStateAxes(a, (id) => id === 'a' ? a : b)).toEqual([]);
    expect(findPrototypeStateAxes(a, () => null)).toEqual([]);
    expect(findPrototypeStateAxes(null, () => a)).toEqual([]);
  });
});
