import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { Api } from '../api/api';
import { deleteSelection, putSelection } from '../api/functions';
import { SelectionReporter } from './selection-reporter';

function harness(fail = false) {
  const invoke = vi.fn(async (_fn: unknown, _params: unknown) => {
    if (fail) throw new Error('offline');
  });
  const reporter = new SelectionReporter({ invoke } as unknown as Api, 'document');
  const puts = () =>
    invoke.mock.calls
      .filter(([fn]) => fn === putSelection)
      .map(([, params]) => params as { id: string; body: { editorId: string; ids: string[] } });
  return { reporter, invoke, puts };
}

describe('selection reporting', () => {
  beforeEach(() => vi.useFakeTimers());
  afterEach(() => {
    vi.clearAllTimers();
    vi.useRealTimers();
  });

  it('reports the first selection, even an empty one, after a pause', async () => {
    const { reporter, puts } = harness();
    reporter.update([]);
    expect(puts()).toHaveLength(0);
    await vi.advanceTimersByTimeAsync(250);
    expect(puts()).toHaveLength(1);
    expect(puts()[0].id).toBe('document');
    expect(puts()[0].body.ids).toEqual([]);
    reporter.close();
  });

  it('sends only the last selection of a burst and skips unchanged ones', async () => {
    const { reporter, puts } = harness();
    reporter.update(['a']);
    await vi.advanceTimersByTimeAsync(100);
    reporter.update(['a', 'b']);
    await vi.advanceTimersByTimeAsync(100);
    reporter.update(['b']);
    await vi.advanceTimersByTimeAsync(250);
    expect(puts().map((p) => p.body.ids)).toEqual([['b']]);

    reporter.update(['b']);
    await vi.advanceTimersByTimeAsync(500);
    expect(puts()).toHaveLength(1);
    reporter.update(['c']);
    await vi.advanceTimersByTimeAsync(250);
    expect(puts().map((p) => p.body.ids)).toEqual([['b'], ['c']]);
    expect(new Set(puts().map((p) => p.body.editorId)).size).toBe(1);
    reporter.close();
  });

  it('renews the last report while open', async () => {
    const { reporter, puts } = harness();
    reporter.update(['a']);
    await vi.advanceTimersByTimeAsync(250);
    await vi.advanceTimersByTimeAsync(10_000);
    expect(puts().map((p) => p.body.ids)).toEqual([['a'], ['a']]);
    reporter.close();
  });

  it('withdraws the selection on close and stops reporting', async () => {
    const { reporter, invoke, puts } = harness();
    reporter.update(['a']);
    await vi.advanceTimersByTimeAsync(250);
    const editorId = puts()[0].body.editorId;
    reporter.close();
    reporter.update(['b']);
    await vi.advanceTimersByTimeAsync(20_000);
    expect(puts()).toHaveLength(1);
    expect(invoke).toHaveBeenLastCalledWith(deleteSelection, { id: 'document', editorId });
  });

  it('survives a server that cannot be reached', async () => {
    const { reporter, invoke } = harness(true);
    reporter.update(['a']);
    await vi.advanceTimersByTimeAsync(250);
    expect(invoke).toHaveBeenCalledTimes(1);
    reporter.close();
  });
});
