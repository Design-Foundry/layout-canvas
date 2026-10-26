/** Whole-document delivery. Native visualcore remains the authority when reopening a file. */
export interface DocumentSnapshot {
  version: number;
  nodes: Record<string, unknown>[];
  modes?: Record<string, unknown>[];
  tokens?: Record<string, unknown>[];
}

export interface DocumentPackage {
  format: 'canvas-document/1';
  name: string;
  snapshot: DocumentSnapshot;
}

// Match the existing atomic server import limit; inline raster pixels count toward it.
export const MAX_DOCUMENT_SNAPSHOT_BYTES = 2 * 1024 * 1024;
export const MAX_DOCUMENT_FILE_BYTES = MAX_DOCUMENT_SNAPSHOT_BYTES + 2048;
const bytes = (text: string): number => new TextEncoder().encode(text).length;
const object = (value: unknown): value is Record<string, unknown> =>
  value !== null && typeof value === 'object' && !Array.isArray(value);

function validateSnapshot(value: unknown): asserts value is DocumentSnapshot {
  if (
    !object(value) ||
    !Number.isInteger(value['version']) ||
    (value['version'] as number) < 1 ||
    (value['version'] as number) > 22 ||
    Object.keys(value).some((key) => !['version', 'nodes', 'modes', 'tokens'].includes(key)) ||
    !Array.isArray(value['nodes']) ||
    value['nodes'].length > 10000 ||
    !value['nodes'].every(object) ||
    ['modes', 'tokens'].some((key) => {
      const list = value[key];
      if (list === undefined) return (value['version'] as number) >= 2;
      return (
        !Array.isArray(list) || list.length > (key === 'modes' ? 100 : 10000) || !list.every(object)
      );
    })
  ) {
    throw new Error('This file is not a supported Canvas document snapshot.');
  }
  if (bytes(JSON.stringify(value)) > MAX_DOCUMENT_SNAPSHOT_BYTES)
    throw new Error('The complete document exceeds the 2 MB local import limit, including images.');
}

/** Includes all authored screens, references, tokens, component definitions and inline images. */
export function exportDocument(snapshot: unknown, name: string): DocumentPackage {
  const trimmed = name.trim();
  if (!trimmed || trimmed.length > 200)
    throw new Error('Use a document name between 1 and 200 characters.');
  // A detached JSON value prevents later edits from changing the exported package.
  let copied: unknown;
  try {
    copied = JSON.parse(JSON.stringify(snapshot));
  } catch {
    throw new Error('The current document could not be serialized.');
  }
  validateSnapshot(copied);
  const result: DocumentPackage = { format: 'canvas-document/1', name: trimmed, snapshot: copied };
  if (bytes(JSON.stringify(result)) > MAX_DOCUMENT_FILE_BYTES)
    throw new Error('The Canvas document file is too large.');
  return result;
}

/** Checks the envelope and bounded shape. The existing server import validates all native fields. */
export function readDocument(text: string): DocumentPackage {
  if (bytes(text) > MAX_DOCUMENT_FILE_BYTES)
    throw new Error('The Canvas document file exceeds the 2 MB local import limit.');
  let value: unknown;
  try {
    value = JSON.parse(text);
  } catch {
    throw new Error('The Canvas document file is not valid JSON.');
  }
  if (
    !object(value) ||
    Object.keys(value).length !== 3 ||
    value['format'] !== 'canvas-document/1' ||
    typeof value['name'] !== 'string' ||
    !Object.hasOwn(value, 'snapshot')
  )
    throw new Error('Choose a complete Canvas document file (.canvas.json).');
  return exportDocument(value['snapshot'], value['name']);
}

export function documentFilename(name: string): string {
  const stem = name
    .replace(/[<>:"/\\|?*\x00-\x1f]/g, '-')
    .replace(/[. ]+$/g, '')
    .trim()
    .slice(0, 100);
  return `${stem || 'canvas-document'}.canvas.json`;
}
