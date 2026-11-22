import { Api } from '../api/api';
import { deleteSelection, putSelection } from '../api/functions';

const DEBOUNCE_MS = 250;
const RENEW_MS = 10_000;

/**
 * Tells the server which nodes this editor has selected, so tools acting on the document can see it.
 * Changes go out after a short pause; the last report is repeated while the editor stays open because
 * the server forgets a selection that is not renewed.
 */
export class SelectionReporter {
  private readonly editorId = crypto.randomUUID();
  private ids: readonly string[] = [];
  private reported = false;
  private closed = false;
  private debounce?: ReturnType<typeof setTimeout>;
  private readonly renew: ReturnType<typeof setInterval>;

  constructor(
    private readonly api: Api,
    private readonly documentId: string,
  ) {
    this.renew = setInterval(() => {
      if (this.reported) void this.send();
    }, RENEW_MS);
  }

  /** Reports the selection once it has stopped changing. The first call always reports. */
  update(ids: readonly string[]): void {
    if (this.closed) return;
    if (this.reported && ids.length === this.ids.length && ids.every((id, i) => id === this.ids[i]))
      return;
    this.ids = [...ids];
    this.reported = true;
    clearTimeout(this.debounce);
    this.debounce = setTimeout(() => void this.send(), DEBOUNCE_MS);
  }

  /** Withdraws the selection. */
  close(): void {
    if (this.closed) return;
    this.closed = true;
    clearTimeout(this.debounce);
    clearInterval(this.renew);
    this.api
      .invoke(deleteSelection, { id: this.documentId, editorId: this.editorId })
      .catch(() => undefined);
  }

  private async send(): Promise<void> {
    try {
      await this.api.invoke(putSelection, {
        id: this.documentId,
        body: { editorId: this.editorId, ids: [...this.ids] },
      });
    } catch {
      // The next change or renewal tries again.
    }
  }
}
