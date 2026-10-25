import { HttpErrorResponse } from '@angular/common/http';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { OutgoingOp, VisualCore } from '@canvas/bridge';
import type { Api } from '../api/api';
import { appendOps, getDocument, getOps } from '../api/functions';
import { DocumentSync } from './document-sync';

const snapshot = { version: 17, nodes: [], tokens: [], modes: [] };
const edit = (id: string): OutgoingOp => ({
  id,
  kind: 'SetProps',
  data: {
    id: 'hotspot',
    patch: {
      interaction: {
        trigger: 'click',
        action: 'open-overlay',
        target: 'modal',
        transition: 'dissolve',
        durationMs: 450,
      },
    },
  },
});
function harness(invoke: (fn: unknown, params: any) => Promise<any>) {
  const outbox: OutgoingOp[] = [];
  const core = {
    loadDocument: vi.fn(() => true),
    applyRemoteOps: vi.fn((ops: unknown[]) => ops.length),
    takeOutbox: vi.fn(() => outbox.splice(0)),
  };
  const api = { invoke: vi.fn(invoke) },
    errors = vi.fn();
  const sync = new DocumentSync(
    api as unknown as Api,
    core as unknown as VisualCore,
    'document',
    errors,
  );
  return { sync, core, api, outbox, errors };
}

describe('document oplog synchronization', () => {
  beforeEach(() => {
    vi.useFakeTimers();
    vi.setSystemTime(0);
  });
  afterEach(() => {
    vi.clearAllTimers();
    vi.useRealTimers();
  });

  it('loads prototype snapshot data and applies paginated operations in sequence order', async () => {
    const remote = { ...edit('remote'), seq: 41 },
      echo = { ...edit('echo'), seq: 42 };
    const after: number[] = [];
    const { sync, core } = harness(async (fn, params) => {
      if (fn === getDocument) return { snapshot: { seq: 40, data: snapshot } };
      expect(fn).toBe(getOps);
      after.push(params.after);
      return params.after === 40 ? { ops: [remote], headSeq: 42 } : { ops: [echo], headSeq: 42 };
    });
    await sync.open();
    expect(core.loadDocument).toHaveBeenCalledWith(snapshot);
    expect(after).toEqual([40, 41]);
    expect(core.applyRemoteOps.mock.calls).toEqual([[[remote]], [[echo]]]);
    await sync.close();
  });

  it('reads remote edits before its own echo instead of jumping to the append response head', async () => {
    const local = edit('local'),
      remote = { ...edit('remote'), seq: 11 },
      echo = { ...local, seq: 12 };
    const reads: number[] = [],
      sent: OutgoingOp[][] = [];
    const { sync, core, outbox } = harness(async (fn, params) => {
      if (fn === getDocument) return { snapshot: { seq: 10, data: snapshot } };
      if (fn === appendOps) {
        sent.push(params.body.ops);
        return { acks: [{ id: local.id, seq: 12 }], headSeq: 12 };
      }
      expect(fn).toBe(getOps);
      reads.push(params.after);
      return reads.length === 1 ? { ops: [], headSeq: 10 } : { ops: [remote, echo], headSeq: 12 };
    });
    await sync.open();
    outbox.push(local);
    await vi.advanceTimersByTimeAsync(200);
    expect(sent).toEqual([[local]]);
    expect(reads).toEqual([10, 10]);
    expect(core.applyRemoteOps).toHaveBeenCalledExactlyOnceWith([remote, echo]);
    expect(core.loadDocument).toHaveBeenCalledTimes(1);
    await sync.close();
  });

  it('continues reading remote prototype changes without replacing the document or producing edits', async () => {
    const remote = { ...edit('remote-overlay-update'), seq: 2 };
    let reads = 0;
    const { sync, core, api } = harness(async (fn) => {
      if (fn === getDocument) return { snapshot: { seq: 1, data: snapshot } };
      expect(fn).toBe(getOps);
      return ++reads === 1 ? { ops: [], headSeq: 1 } : { ops: [remote], headSeq: 2 };
    });
    await sync.open();
    await vi.advanceTimersByTimeAsync(1000);
    expect(core.applyRemoteOps).toHaveBeenCalledExactlyOnceWith([remote]);
    expect(core.loadDocument).toHaveBeenCalledTimes(1);
    expect(api.invoke.mock.calls.some(([fn]) => fn === appendOps)).toBe(false);
    await sync.close();
  });

  it('retries the same operation identities after a failed post and retains subsequent edits', async () => {
    const first = edit('first'),
      second = edit('second');
    const attempts: OutgoingOp[][] = [];
    const { sync, outbox, errors } = harness(async (fn, params) => {
      if (fn === getDocument) return { snapshot: { seq: 0, data: snapshot } };
      if (fn === getOps) return { ops: [], headSeq: 0 };
      expect(fn).toBe(appendOps);
      attempts.push([...params.body.ops]);
      if (attempts.length === 1) throw new HttpErrorResponse({ status: 503 });
      return {
        acks: params.body.ops.map((op: OutgoingOp, index: number) => ({
          id: op.id,
          seq: index + 1,
        })),
        headSeq: 2,
      };
    });
    await sync.open();
    outbox.push(first);
    await vi.advanceTimersByTimeAsync(200);
    expect(errors).toHaveBeenLastCalledWith(true);
    outbox.push(second);
    await vi.advanceTimersByTimeAsync(2000);
    expect(attempts).toEqual([[first], [first, second]]);
    expect(errors).toHaveBeenLastCalledWith(false);
    await sync.close();
  });

  it('reloads a compacted log and preserves unsent edits produced while the pull was in flight', async () => {
    const late = edit('late'),
      echo = { ...late, seq: 21 };
    let loads = 0,
      reads = 0;
    const sent: OutgoingOp[][] = [];
    const h = harness(async (fn, params) => {
      if (fn === getDocument) return { snapshot: { seq: ++loads === 1 ? 1 : 20, data: snapshot } };
      if (fn === appendOps) {
        sent.push(params.body.ops);
        return { acks: [{ id: late.id, seq: 21 }], headSeq: 21 };
      }
      expect(fn).toBe(getOps);
      if (++reads === 2) {
        h.outbox.push(late);
        throw new HttpErrorResponse({ status: 410 });
      }
      return { ops: reads === 4 ? [echo] : [], headSeq: loads === 1 ? 1 : 21 };
    });
    await h.sync.open();
    await vi.advanceTimersByTimeAsync(1000);
    expect(h.core.loadDocument).toHaveBeenCalledTimes(2);
    await vi.advanceTimersByTimeAsync(200);
    expect(sent).toEqual([[late]]);
    expect(h.core.applyRemoteOps).toHaveBeenCalledExactlyOnceWith([echo]);
    expect(h.errors).toHaveBeenLastCalledWith(false);
    await h.sync.close();
  });
});
