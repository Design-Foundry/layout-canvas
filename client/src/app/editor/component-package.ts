/** Portable component data. Native visualcore validates every field before server storage. */
export interface SnapshotNode {
  id: string;
  type: string;
  order: string;
  parent?: string;
  component?: string;
  [key: string]: any;
}

export interface ComponentSnapshot {
  version: number;
  nodes: SnapshotNode[];
  modes: Record<string, any>[];
  tokens: Record<string, any>[];
}

export interface ComponentPackage {
  format: 'canvas-component/1';
  name: string;
  componentId: string;
  snapshot: ComponentSnapshot;
}

export const MAX_COMPONENT_BYTES = 2 * 1024 * 1024;
const UUID = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;
const NODE_TYPES = new Set([
  'frame',
  'rect',
  'ellipse',
  'component',
  'instance',
  'text',
  'line',
  'path',
  'image',
]);

function object(value: unknown): value is Record<string, any> {
  return !!value && typeof value === 'object' && !Array.isArray(value);
}

function snapshot(value: unknown): asserts value is ComponentSnapshot {
  if (
    !object(value) ||
    ![2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18].includes(value['version']) ||
    !Array.isArray(value['nodes']) ||
    !Array.isArray(value['tokens']) ||
    !Array.isArray(value['modes']) ||
    value['nodes'].length > 10000 ||
    value['tokens'].length > 10000 ||
    value['modes'].length > 100
  ) {
    throw new Error('This file is not a supported Canvas component snapshot.');
  }
  // Prototype destinations refer to authored root frames in the current document.
  if (
    value['nodes'].some((node: any) => {
      const metadata = (fields: any) =>
        object(fields) &&
        (Object.hasOwn(fields, 'interaction') || Object.hasOwn(fields, 'prototypeStart'));
      return (
        metadata(node) ||
        [...(node?.overrides ?? []), ...(node?.definition?.rules ?? [])].some((item: any) =>
          metadata(item?.patch),
        )
      );
    })
  )
    throw new Error(
      'Prototype interactions and starting screens are document-local and cannot travel in Canvas component files. Remove prototype metadata before importing or exporting this component.',
    );
  const ids = new Set<string>();
  const imageFields = ['image', 'imageFit', 'imageCrop'];
  const rasterIds = new Set<string>();
  let rasterBytes = 0;
  const validRaster = (image: any): boolean => {
    if (
      !object(image) ||
      Object.keys(image).length !== 4 ||
      !Number.isInteger(image['width']) ||
      !Number.isInteger(image['height']) ||
      image['width'] < 1 ||
      image['height'] < 1 ||
      image['width'] > 512 ||
      image['height'] > 512 ||
      typeof image['id'] !== 'string' ||
      !/^[0-9a-f]{64}$/.test(image['id']) ||
      typeof image['pixels'] !== 'string' ||
      image['pixels'].length !== 4 * Math.ceil((image['width'] * image['height'] * 4) / 3)
    )
      return false;
    try {
      const decoded = atob(image['pixels']);
      if (
        decoded.length !== image['width'] * image['height'] * 4 ||
        btoa(decoded) !== image['pixels']
      )
        return false;
    } catch {
      return false;
    }
    rasterIds.add(image['id']);
    rasterBytes += image['pixels'].length;
    return true;
  };
  const validImageFields = (fields: any): boolean => {
    if (fields?.image !== undefined && !validRaster(fields.image)) return false;
    if (
      fields?.imageFit !== undefined &&
      !['cover', 'contain', 'stretch'].includes(fields.imageFit)
    )
      return false;
    const crop = fields?.imageCrop;
    if (
      crop !== undefined &&
      (!object(crop) ||
        Object.keys(crop).length !== 4 ||
        !['x', 'y', 'w', 'h'].every(
          (key) => typeof crop[key] === 'number' && Number.isFinite(crop[key]),
        ) ||
        crop['x'] < 0 ||
        crop['y'] < 0 ||
        crop['w'] <= 0 ||
        crop['h'] <= 0 ||
        crop['x'] + crop['w'] > 1 ||
        crop['y'] + crop['h'] > 1)
    )
      return false;
    return true;
  };
  for (const node of value['nodes']) {
    const patches = [...(node?.overrides ?? []), ...(node?.definition?.rules ?? [])];
    if (
      value['version'] < 15 &&
      (node?.type === 'image' ||
        imageFields.some((key) => node?.[key] !== undefined) ||
        patches.some((item: any) => imageFields.some((key) => item?.patch?.[key] !== undefined)))
    )
      throw new Error('Native images require Canvas snapshot version 15.');
    if (
      (node?.type === 'image' && node?.image === undefined) ||
      (node?.type !== 'image' && imageFields.some((key) => node?.[key] !== undefined)) ||
      !validImageFields(node) ||
      patches.some((item: any) => !validImageFields(item?.patch))
    )
      throw new Error('Invalid native image, fit or crop fields.');
  }
  if (rasterIds.size > 8 || rasterBytes > 1400000)
    throw new Error('The component exceeds the native image document budget.');

  const validPoint = (point: any) =>
    object(point) &&
    Object.keys(point).every((key) => key === 'x' || key === 'y') &&
    [point['x'], point['y']].every(
      (v: any) => typeof v === 'number' && Number.isFinite(v) && v >= 0 && v <= 1,
    );
  const validPath = (path: any) => {
    if (
      !object(path) ||
      !Object.keys(path).every((key) =>
        ['points', 'closed', 'inHandles', 'outHandles', 'filled'].includes(key),
      ) ||
      typeof path['closed'] !== 'boolean' ||
      !Array.isArray(path['points']) ||
      path['points'].length < (path['closed'] ? 3 : 2) ||
      path['points'].length > 64 ||
      !path['points'].every(validPoint) ||
      (path['filled'] !== undefined && typeof path['filled'] !== 'boolean') ||
      (path['filled'] && !path['closed'])
    )
      return false;
    const incoming = path['inHandles'] ?? [],
      outgoing = path['outHandles'] ?? [];
    return (
      Array.isArray(incoming) &&
      Array.isArray(outgoing) &&
      incoming.length === outgoing.length &&
      (incoming.length === 0 || incoming.length === path['points'].length) &&
      incoming.every(validPoint) &&
      outgoing.every(validPoint)
    );
  };
  const curvedPath = (path: any) =>
    path?.filled === true || path?.inHandles?.length || path?.outHandles?.length;
  for (const node of value['nodes']) {
    const patches = [...(node?.overrides ?? []), ...(node?.definition?.rules ?? [])];
    if (
      value['version'] < 14 &&
      (curvedPath(node?.path) || patches.some((item: any) => curvedPath(item?.patch?.path)))
    )
      throw new Error('Curved or filled paths require Canvas snapshot version 14.');
    if (
      value['version'] < 13 &&
      (node?.type === 'path' || patches.some((item: any) => item?.patch?.path !== undefined))
    )
      throw new Error('Native paths require Canvas snapshot version 13.');
    if (
      (node?.type === 'path' && !validPath(node.path)) ||
      (node?.type !== 'path' && node?.path !== undefined)
    )
      throw new Error(
        'Path geometry requires 2 to 64 normalized vertices, or at least 3 for closed paths.',
      );
    for (const item of patches) {
      if (item?.patch?.path !== undefined && !validPath(item.patch.path))
        throw new Error('Invalid path override geometry.');
    }
  }
  if (value['version'] < 12 && value['nodes'].some((node: any) => node?.type === 'line'))
    throw new Error('Native lines require Canvas snapshot version 12.');
  if (
    value['version'] < 11 &&
    (value['tokens'].some((token: any) => token?.type === 'typography') ||
      value['nodes'].some((node: any) => node?.typography))
  )
    throw new Error('Shared typography requires Canvas snapshot version 11.');
  for (const node of value['nodes']) {
    if (
      (node?.type !== 'line' && node?.lineReverse !== undefined) ||
      (!['line', 'path'].includes(node?.type) && node?.lineWidth !== undefined)
    )
      throw new Error('Line-only fields cannot be applied to another layer type.');
    if (
      ['line', 'path'].includes(node?.type) &&
      ((node.lineWidth !== undefined &&
        (typeof node.lineWidth !== 'number' ||
          !Number.isFinite(node.lineWidth) ||
          node.lineWidth < 0 ||
          node.lineWidth > 4096)) ||
        (node.lineReverse !== undefined && typeof node.lineReverse !== 'boolean'))
    )
      throw new Error('The line has an invalid width or direction.');
    if (
      [...(node?.overrides ?? []), ...(node?.definition?.rules ?? [])].some(
        (item: any) => item?.patch?.typography !== undefined,
      )
    )
      throw new Error(
        'Typography bindings belong to source text layers; copied-layer and variant binding changes are unsupported.',
      );
    if (
      node?.typography &&
      (node.type !== 'text' ||
        !value['tokens'].some(
          (token: any) => token?.id === node.typography && token.type === 'typography',
        ))
    )
      throw new Error('A text layer refers to a missing or incompatible typography token.');
  }
  for (const node of value['nodes']) {
    const slanted = (font: unknown) =>
      font === 'inter-oblique-v1' || font === 'inter-bold-oblique-v1';
    if (
      value['version'] < 10 &&
      (slanted(node['text']?.font) ||
        [...(node['overrides'] ?? []), ...(node['definition']?.rules ?? [])].some(
          (item: any) => slanted(item?.patch?.textStyle?.font) || slanted(item?.patch?.text?.font),
        ))
    )
      throw new Error('Oblique faces require Canvas snapshot version 10.');
    if (
      value['version'] < 9 &&
      ((node['text']?.font && node['text'].font !== 'inter-regular-v1') ||
        [...(node['overrides'] ?? []), ...(node['definition']?.rules ?? [])].some(
          (item: any) =>
            item?.patch?.textStyle?.font !== undefined ||
            (item?.patch?.text?.font && item.patch.text.font !== 'inter-regular-v1'),
        ))
    )
      throw new Error('Font selection requires Canvas snapshot version 9.');
    if (
      value['version'] < 8 &&
      (node['textWrap'] === true ||
        [...(node['overrides'] ?? []), ...(node['definition']?.rules ?? [])].some(
          (item: any) => item?.patch?.textWrap !== undefined,
        ))
    )
      throw new Error('Text wrapping requires Canvas snapshot version 8.');
    const styles = [...(node['overrides'] ?? []), ...(node['definition']?.rules ?? [])].map(
      (item: any) => item?.patch?.textStyle,
    );
    if (value['version'] < 7 && styles.some((style) => style && Object.keys(style).length !== 4))
      throw new Error('Partial text styles require Canvas snapshot version 7.');
    if (
      (node['definition']?.texts?.length ||
        node['overrides']?.some((o: any) => o?.patch?.textStyle) ||
        node['definition']?.rules?.some((r: any) => r?.patch?.textStyle)) &&
      value['version'] < 6
    )
      throw new Error('Declared text properties require Canvas snapshot version 6.');
    if (
      (node['type'] === 'text' ||
        node['overrides']?.some((o: any) => o?.patch?.text) ||
        node['definition']?.rules?.some((r: any) => r?.patch?.text)) &&
      value['version'] < 5
    )
      throw new Error('Native text requires Canvas snapshot version 5.');
    if (node['definition']?.swaps?.length && value['version'] < 4)
      throw new Error('Instance swap properties require Canvas snapshot version 4.');
    if (
      value['version'] < 3 &&
      (node['definition'] || Object.keys(node['componentValues'] ?? {}).length)
    )
      throw new Error('Component properties and variants require Canvas snapshot version 3.');
    if (
      !object(node) ||
      !UUID.test(node['id']) ||
      !NODE_TYPES.has(node['type']) ||
      typeof node['order'] !== 'string' ||
      ids.has(node['id'])
    ) {
      throw new Error('The component has invalid or duplicate layers.');
    }
    ids.add(node['id']);
  }
  const byId = new Map(value['nodes'].map((node: SnapshotNode) => [node.id, node]));
  for (const node of value['nodes']) {
    const patches = [...(node?.overrides ?? []), ...(node?.definition?.rules ?? [])];
    for (const item of patches) {
      const target = byId.get(item?.target ?? item?.node);
      if (imageFields.some((key) => item?.patch?.[key] !== undefined) && target?.type !== 'image')
        throw new Error('Image overrides must target native image layers.');
      if (item?.patch?.path !== undefined && target?.type !== 'path')
        throw new Error('Path overrides must target native path layers.');
    }
  }
  for (const node of value['nodes'])
    for (const swap of node['definition']?.swaps ?? []) {
      if (
        !object(swap) ||
        !UUID.test(swap['target']) ||
        byId.get(swap['target'])?.type !== 'instance' ||
        !Array.isArray(swap['choices']) ||
        !swap['choices'].length ||
        swap['choices'].length > 32 ||
        !swap['choices'].includes(swap['default']) ||
        swap['choices'].some(
          (id: unknown) => typeof id !== 'string' || byId.get(id)?.type !== 'component',
        )
      )
        throw new Error(
          'An instance swap property has invalid targets or missing component choices.',
        );
    }
  for (const node of value['nodes']) {
    if (
      (node.parent && !byId.has(node.parent)) ||
      (node.type === 'instance' && byId.get(node.component)?.type !== 'component')
    ) {
      throw new Error('The component is missing a parent or a main component.');
    }
    // Parent and instance dependencies must form a DAG before native snapshot parsing.
    const active = new Set<string>();
    const done = new Set<string>();
    const visit = (id: string, depth: number): void => {
      if (active.has(id) || depth > 128)
        throw new Error('The component contains cyclic or deeply nested layers.');
      if (done.has(id)) return;
      active.add(id);
      const current = byId.get(id)!;
      if (current.parent) visit(current.parent, depth + 1);
      if (current.type === 'instance') visit(current.component!, depth + 1);
      active.delete(id);
      done.add(id);
    };
    visit(node.id, 0);
  }
  const dependencies = new Map<string, string[]>();
  for (const node of value['nodes'])
    for (const swap of node['definition']?.swaps ?? [])
      dependencies.set(node.id, [...(dependencies.get(node.id) ?? []), ...swap['choices']]);
  for (const node of value['nodes']) {
    if (node.type !== 'instance') continue;
    let parent = node.parent;
    while (parent) {
      const ancestor = byId.get(parent)!;
      if (ancestor.type === 'component')
        dependencies.set(parent, [...(dependencies.get(parent) ?? []), node.component!]);
      parent = ancestor.parent;
    }
  }
  const activeComponents = new Set<string>();
  const visitedComponents = new Set<string>();
  const visitComponent = (id: string, depth: number): void => {
    if (activeComponents.has(id) || depth > 128)
      throw new Error('Components cannot contain recursive instances.');
    if (visitedComponents.has(id)) return;
    activeComponents.add(id);
    for (const dependency of dependencies.get(id) ?? []) visitComponent(dependency, depth + 1);
    activeComponents.delete(id);
    visitedComponents.add(id);
  };
  for (const id of dependencies.keys()) visitComponent(id, 0);
  for (const collection of [value['tokens'], value['modes']]) {
    const seen = new Set<string>();
    for (const entry of collection) {
      if (!object(entry) || !UUID.test(entry['id']) || seen.has(entry['id'])) {
        throw new Error('The component has invalid or duplicate tokens or modes.');
      }
      seen.add(entry['id']);
    }
  }
}

/** Excludes unrelated artwork; retains component dependencies and the document's token system. */
export function exportComponent(value: unknown, componentId: string): ComponentPackage {
  snapshot(value);
  const nodes = new Map(value.nodes.map((node) => [node.id, node]));
  const root = nodes.get(componentId);
  if (root?.type !== 'component') throw new Error('Select a main component to export.');
  const children = new Map<string, SnapshotNode[]>();
  for (const node of value.nodes) {
    if (node.parent) children.set(node.parent, [...(children.get(node.parent) ?? []), node]);
  }
  const included = new Set<string>();
  const include = (id: string): void => {
    if (included.has(id)) return;
    included.add(id);
    const node = nodes.get(id)!;
    for (const swap of node['definition']?.swaps ?? [])
      for (const choice of swap['choices']) include(choice);
    if (node.type === 'instance') include(node.component!);
    for (const child of children.get(id) ?? []) include(child.id);
  };
  include(componentId);
  const selected = structuredClone(value.nodes.filter((node) => included.has(node.id)));
  let dependencyX = Number(root['w']) || 200;
  for (const node of selected) {
    if (node.id === componentId || !included.has(node.parent ?? '')) {
      delete node.parent;
      node['x'] = node.id === componentId ? 0 : dependencyX + 80;
      node['y'] = 0;
      if (node.id !== componentId) dependencyX = node['x'] + (Number(node['w']) || 200);
    }
  }
  const result: ComponentPackage = {
    format: 'canvas-component/1',
    name: String(root['name'] || 'Component').slice(0, 200),
    componentId,
    snapshot: {
      version: value.version,
      nodes: selected,
      modes: structuredClone(value.modes),
      tokens: structuredClone(value.tokens),
    },
  };
  if (new TextEncoder().encode(JSON.stringify(result)).length > MAX_COMPONENT_BYTES) {
    throw new Error('This component exceeds the 2 MB file limit.');
  }
  return result;
}

export function readComponent(text: string): ComponentPackage {
  if (new TextEncoder().encode(text).length > MAX_COMPONENT_BYTES)
    throw new Error('The component file exceeds 2 MB.');
  let value: unknown;
  try {
    value = JSON.parse(text);
  } catch {
    throw new Error('The component file is not valid JSON.');
  }
  if (
    !object(value) ||
    value['format'] !== 'canvas-component/1' ||
    typeof value['name'] !== 'string' ||
    !value['name'].trim() ||
    value['name'].length > 200 ||
    !UUID.test(value['componentId'])
  )
    throw new Error('Choose a Canvas component file.');
  snapshot(value['snapshot']);
  if (
    !value['snapshot'].nodes.some(
      (node) => node.id === value['componentId'] && node.type === 'component' && !node.parent,
    )
  ) {
    throw new Error('The file has no top-level main component.');
  }
  // Re-extract to discard unrelated artwork even in a manually edited file.
  return exportComponent(value['snapshot'], value['componentId']);
}
