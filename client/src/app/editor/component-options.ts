import type { ComponentSnapshot } from './component-package';

/** Main-component choices which cannot introduce a composition cycle for this instance. */
export function componentOptions(
  snapshot: ComponentSnapshot,
  instanceId: string,
): { label: string; value: string }[] {
  const byId = new Map(snapshot.nodes.map((node) => [node.id, node]));
  let parent = byId.get(instanceId)?.parent;
  let host: string | undefined;
  const parents = new Set<string>();
  while (parent && !parents.has(parent)) {
    parents.add(parent);
    const node = byId.get(parent);
    if (node?.type === 'component') {
      host = node.id;
      break;
    }
    parent = node?.parent;
  }
  const children = new Map<string, typeof snapshot.nodes>();
  for (const node of snapshot.nodes) {
    if (node.parent) children.set(node.parent, [...(children.get(node.parent) ?? []), node]);
  }
  const usesHost = (id: string, visiting = new Set<string>()): boolean => {
    if (id === host) return true;
    if (visiting.has(id) || visiting.size > 128) return true;
    const path = new Set(visiting).add(id);
    const node = byId.get(id);
    if (
      (node?.['definition']?.swaps ?? []).some((swap: { choices: string[] }) =>
        swap.choices.some((choice) => usesHost(choice, path)),
      )
    )
      return true;
    if (node?.type === 'instance' && node.component && usesHost(node.component, path)) return true;
    return (children.get(id) ?? []).some((child) => usesHost(child.id, path));
  };
  return snapshot.nodes
    .filter((node) => node.type === 'component' && (!host || !usesHost(node.id)))
    .map((node) => ({ label: node['name'] || 'Component', value: node.id }))
    .sort((a, b) => a.label.localeCompare(b.label) || a.value.localeCompare(b.value));
}
