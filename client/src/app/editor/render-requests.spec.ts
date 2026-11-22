import { describe, expect, it, vi } from 'vitest';
import type { ExportedImage } from '@canvas/bridge';
import type { Api } from '../api/api';
import { getRenderRequest, putRenderResult } from '../api/functions';
import { RenderRequestPoller, answerRenderRequest } from './render-requests';

function image(width: number, height: number, scale: number): ExportedImage {
  return { width, height, scale, requestedScale: scale, clamped: false, tiled: false, pixels: new Uint8Array(width * height * 4) };
}

const request = { id: 'request', nodeId: 'node', scale: 2, maxSize: 2048 };

describe('answering a render request', () => {
  it('renders at the requested scale and returns a PNG', async () => {
    const exportNode = vi.fn(async () => image(8, 4, 2));

    const result = await answerRenderRequest(request, exportNode, 'editor');

    expect(exportNode).toHaveBeenCalledExactlyOnceWith('node', 2);
    expect(result).toMatchObject({ editorId: 'editor', width: 8, height: 4, scale: 2, error: null });
    expect(atob(result.png!).slice(1, 4)).toBe('PNG');
  });

  it('lowers the scale when the longest side is over the limit', async () => {
    const exportNode = vi.fn(async (_id: string, scale: number) =>
      scale > 2.5 ? image(3000, 10, 3) : image(1999, 7, scale),
    );

    const result = await answerRenderRequest({ ...request, scale: 3 }, exportNode, 'editor');

    expect(exportNode).toHaveBeenCalledTimes(2);
    expect(exportNode.mock.calls[1][1]).toBeCloseTo(3 * (2048 / 3000) * 0.999);
    expect(result.width).toBe(1999);
    expect(result.error).toBeNull();
  });

  it('reports the editor failure as an error result', async () => {
    const result = await answerRenderRequest(request, async () => Promise.reject(new Error('node not found')), 'editor');

    expect(result).toMatchObject({ png: null, error: 'node not found' });
  });
});

describe('the render request poller', () => {
  it('polls with its editor id, answers each request and stops when closed', async () => {
    const answers: unknown[] = [];
    let poll = 0;
    let poller!: RenderRequestPoller;
    const invoke = vi.fn(async (fn: unknown, params: unknown) => {
      if (fn === getRenderRequest) {
        poll++;
        if (poll === 2) poller.close();
        return poll === 1 ? request : null;
      }
      if (fn === putRenderResult) answers.push(params);
      return undefined;
    });
    poller = new RenderRequestPoller({ invoke } as unknown as Api, 'document', 'editor', async () => image(2, 2, 2));

    poller.start();
    await vi.waitFor(() => expect(poll).toBe(2));

    expect(invoke.mock.calls[0][1]).toMatchObject({ id: 'document', editorId: 'editor' });
    expect(answers).toHaveLength(1);
    expect(answers[0]).toMatchObject({ id: 'document', requestId: 'request', body: { width: 2, height: 2, error: null } });
    expect(poll).toBe(2);
  });

  it('retries after a failed poll', async () => {
    let poll = 0;
    let poller!: RenderRequestPoller;
    const invoke = vi.fn(async () => {
      poll++;
      if (poll === 1) throw new Error('offline');
      poller.close();
      return null;
    });
    poller = new RenderRequestPoller({ invoke } as unknown as Api, 'document', 'editor', async () => image(1, 1, 1));

    vi.useFakeTimers();
    poller.start();
    await vi.advanceTimersByTimeAsync(3500);
    vi.useRealTimers();

    expect(poll).toBe(2);
  });
});
