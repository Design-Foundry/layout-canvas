import type { NodeProps, VariantAxis } from '@canvas/bridge';

/** Find the nearest reusable component context, never an unrelated outer component. */
export function findPrototypeStateAxes(selected: NodeProps | null, lookup: (id: string) => NodeProps | null): readonly VariantAxis[] {
  const visited = new Set<string>();
  for (let node = selected; node && !visited.has(node.id); node = node.parent ? lookup(node.parent) : null) {
    visited.add(node.id);
    if (node.type === 'component' || node.type === 'instance')
      return node.definition?.axes ?? (node.component ? lookup(node.component)?.definition?.axes : undefined) ?? [];
  }
  return [];
}
