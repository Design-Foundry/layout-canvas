import type { ExportedImage } from '@canvas/bridge';
import { Api } from '../api/api';
import { getRenderRequest, putRenderResult } from '../api/functions';
import type { RenderRequest } from '../api/models/render-request';
import type { RenderResult } from '../api/models/render-result';
import { encodePng } from './export-file';

const WAIT_SECONDS = 20;
const RETRY_MS = 3000;
const BASE64_CHUNK = 0x8000;

export type NodeExporter = (id: string, scale: number) => Promise<ExportedImage>;

async function toBase64(blob: Blob): Promise<string> {
  const bytes = new Uint8Array(await blob.arrayBuffer());
  let binary = '';
  for (let i = 0; i < bytes.length; i += BASE64_CHUNK)
    binary += String.fromCharCode(...bytes.subarray(i, i + BASE64_CHUNK));
  return btoa(binary);
}

/**
 * Renders the node a request names with the export path, lowering the scale until the longest side fits
 * `maxSize`. Failures come back as an error result instead of throwing.
 */
export async function answerRenderRequest(
  request: RenderRequest,
  exportNode: NodeExporter,
  editorId: string,
): Promise<RenderResult> {
  try {
    let image = await exportNode(request.nodeId, request.scale);
    for (let attempt = 0; attempt < 3 && Math.max(image.width, image.height) > request.maxSize; attempt++) {
      const fit = (request.maxSize / Math.max(image.width, image.height)) * 0.999;
      image = await exportNode(request.nodeId, image.scale * fit);
    }
    if (Math.max(image.width, image.height) > request.maxSize) throw new Error('node is too large to render');
    const png = await toBase64(await encodePng(image.width, image.height, image.pixels));
    return { editorId, png, width: image.width, height: image.height, scale: image.scale, error: null };
  } catch (reason) {
    const error = reason instanceof Error ? reason.message : 'render failed';
    return { editorId, png: null, width: 0, height: 0, scale: 0, error };
  }
}

/**
 * Answers render requests for this document. It long-polls the server, which only hands requests to the
 * editor that is live for the document, renders each one offscreen and posts the PNG back.
 */
export class RenderRequestPoller {
  private closed = false;

  constructor(
    private readonly api: Api,
    private readonly documentId: string,
    private readonly editorId: string,
    private readonly exportNode: NodeExporter,
  ) {}

  start(): void {
    void this.run();
  }

  close(): void {
    this.closed = true;
  }

  private async run(): Promise<void> {
    while (!this.closed) {
      try {
        const request = await this.api.invoke(getRenderRequest, {
          id: this.documentId,
          editorId: this.editorId,
          wait: WAIT_SECONDS,
        });
        if (this.closed || !request) continue;
        const body = await answerRenderRequest(request, this.exportNode, this.editorId);
        await this.api.invoke(putRenderResult, { id: this.documentId, requestId: request.id, body });
      } catch {
        await new Promise((resolve) => setTimeout(resolve, RETRY_MS));
      }
    }
  }
}
