import { HttpErrorResponse } from '@angular/common/http';
import { OutgoingOp, VisualCore } from '@canvas/bridge';
import { Api } from '../api/api';
import { appendOps, getDocument, getOps } from '../api/functions';

const SYNC_INTERVAL_MS = 200;
const PULL_INTERVAL_MS = 1000;
const RETRY_DELAY_MS = 2000;
const MAX_OPS_PER_REQUEST = 1000;
const OPS_PAGE_SIZE = 5000;

/**
 * Keeps one document in visualcore in step with the server: loads the snapshot and the ops after it,
 * posts local edits in batches and reads the log for everyone else's. Ops carry client ids, so a
 * failed batch is simply sent again, and visualcore recognizes its own edits when they come back. The
 * server checks every op against its own copy of the document; if it refuses one, the local document
 * has drifted and is loaded again.
 */
export class DocumentSync {
  /** The last log entry applied locally. Only reading the log moves it. */
  private seq = 0;
  private pending: OutgoingOp[] = [];
  private busy = false;
  private retryAt = 0;
  private nextPullAt = 0;
  private timer?: ReturnType<typeof setInterval>;

  constructor(
    private readonly api: Api,
    private readonly core: VisualCore,
    private readonly documentId: string,
    private readonly onError: (failing: boolean) => void,
  ) {}

  async open(): Promise<void> {
    await this.load();
    this.timer = setInterval(() => void this.sync(), SYNC_INTERVAL_MS);
  }

  /** Sends what is left and stops. */
  async close(): Promise<void> {
    clearInterval(this.timer);
    while (this.busy) await new Promise((resolve) => setTimeout(resolve, SYNC_INTERVAL_MS));
    await this.push();
  }

  private async load(): Promise<void> {
    const document = await this.api.invoke(getDocument, { id: this.documentId });
    if (!this.core.loadDocument(document.snapshot?.data ?? null)) {
      throw new Error('visualcore rejected the document snapshot');
    }
    this.seq = document.snapshot?.seq ?? 0;
    await this.pull();
  }

  private async sync(): Promise<void> {
    if (this.busy || Date.now() < this.retryAt) return;
    this.busy = true;
    try {
      // Reading right after a push confirms our own edits quickly.
      const pushed = await this.push();
      if (pushed === 'refused') {
        console.warn('the server refused some edits, reloading the document');
        await this.reload();
      } else if (pushed === 'sent' || Date.now() >= this.nextPullAt) {
        await this.pullOrReload();
      }
      this.onError(false);
    } catch (error) {
      this.retryAt = Date.now() + RETRY_DELAY_MS;
      this.onError(true);
      if (!(error instanceof HttpErrorResponse)) console.error(error);
    } finally {
      this.busy = false;
    }
  }

  /** Posts local edits. Says whether any went out and whether the server refused one. */
  private async push(): Promise<'none' | 'sent' | 'refused'> {
    this.pending.push(...this.core.takeOutbox());
    if (this.pending.length === 0) return 'none';
    let refused = false;
    while (this.pending.length > 0) {
      const batch = this.pending.slice(0, MAX_OPS_PER_REQUEST);
      const response = await this.api.invoke(appendOps, {
        id: this.documentId,
        body: { ops: batch },
      });
      this.pending.splice(0, batch.length);
      refused ||= response.acks.some((ack) => ack.seq == null);
    }
    return refused ? 'refused' : 'sent';
  }

  private async pull(): Promise<void> {
    this.nextPullAt = Date.now() + PULL_INTERVAL_MS;
    while (true) {
      const page = await this.api.invoke(getOps, {
        id: this.documentId,
        after: this.seq,
        limit: OPS_PAGE_SIZE,
      });
      if (page.ops.length === 0) return;
      if (this.core.applyRemoteOps(page.ops) < 0) {
        throw new Error('visualcore rejected ops from the server');
      }
      this.seq = page.ops[page.ops.length - 1].seq;
      if (this.seq >= page.headSeq) return;
    }
  }

  /** Reads the log, or reopens the document when the ops we still need were folded into a snapshot. */
  private async pullOrReload(): Promise<void> {
    try {
      await this.pull();
    } catch (error) {
      if (!(error instanceof HttpErrorResponse) || error.status !== 410) throw error;
      await this.reload();
    }
  }

  /** Opens the document again. Edits made since the last push are kept and sent afterwards. */
  private async reload(): Promise<void> {
    this.pending.push(...this.core.takeOutbox());
    await this.load();
  }
}
