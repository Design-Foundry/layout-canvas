import { HttpErrorResponse } from '@angular/common/http';
import { OutgoingOp, VisualCore } from '@canvas/bridge';
import { Api } from '../api/api';
import { appendOps, getDocument, getOps, saveSnapshot } from '../api/functions';

const PUSH_INTERVAL_MS = 200;
const RETRY_DELAY_MS = 2000;
const MAX_OPS_PER_REQUEST = 1000;
const OPS_PAGE_SIZE = 5000;
const OPS_PER_SNAPSHOT = 200;

/**
 * Keeps one document in visualcore in step with the server: loads the snapshot and the ops after it,
 * then posts local edits in batches. Ops carry client ids, so a failed batch is simply sent again.
 */
export class DocumentSync {
  private seq = 0;
  private pending: OutgoingOp[] = [];
  private inFlight = false;
  private retryAt = 0;
  private opsSinceSnapshot = 0;
  private timer?: ReturnType<typeof setInterval>;

  constructor(
    private readonly api: Api,
    private readonly core: VisualCore,
    private readonly documentId: string,
    private readonly onError: (failing: boolean) => void,
  ) {}

  async open(): Promise<void> {
    const document = await this.api.invoke(getDocument, { id: this.documentId });
    if (!this.core.loadDocument(document.snapshot?.data ?? null)) {
      throw new Error('visualcore rejected the document snapshot');
    }
    this.seq = document.snapshot?.seq ?? 0;
    this.opsSinceSnapshot = document.headSeq - this.seq;
    await this.pull();
    this.timer = setInterval(() => void this.push(), PUSH_INTERVAL_MS);
  }

  /** Sends what is left and stops. */
  async close(): Promise<void> {
    clearInterval(this.timer);
    this.retryAt = 0;
    await this.push();
  }

  private async pull(): Promise<void> {
    while (true) {
      const page = await this.api.invoke(getOps, { id: this.documentId, after: this.seq, limit: OPS_PAGE_SIZE });
      if (page.ops.length === 0) return;
      if (this.core.applyRemoteOps(page.ops) < 0) {
        throw new Error('visualcore rejected ops from the server');
      }
      this.seq = page.ops[page.ops.length - 1].seq;
      if (this.seq >= page.headSeq) return;
    }
  }

  private async push(): Promise<void> {
    if (this.inFlight || Date.now() < this.retryAt) return;
    this.pending.push(...this.core.takeOutbox());
    if (this.pending.length === 0) return;

    this.inFlight = true;
    try {
      while (this.pending.length > 0) {
        const batch = this.pending.slice(0, MAX_OPS_PER_REQUEST);
        const response = await this.api.invoke(appendOps, { id: this.documentId, body: { ops: batch } });
        this.pending.splice(0, batch.length);
        this.seq = Math.max(this.seq, response.headSeq);
        this.opsSinceSnapshot += batch.length;
      }
      this.onError(false);
      await this.maybeSnapshot();
    } catch (error) {
      this.retryAt = Date.now() + RETRY_DELAY_MS;
      this.onError(true);
      if (!(error instanceof HttpErrorResponse)) console.error(error);
    } finally {
      this.inFlight = false;
    }
  }

  private async maybeSnapshot(): Promise<void> {
    if (this.opsSinceSnapshot < OPS_PER_SNAPSHOT || this.pending.length > 0) return;
    const local = this.core.takeOutbox();
    if (local.length > 0) {
      this.pending.push(...local);
      return;
    }
    try {
      await this.api.invoke(saveSnapshot, { id: this.documentId, body: { seq: this.seq, data: this.core.snapshot() } });
      this.opsSinceSnapshot = 0;
    } catch (error) {
      console.warn('snapshot not saved', error);
    }
  }
}
