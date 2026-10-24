using System.Collections.Concurrent;
using Canvas.Server.VisualCore;
using Microsoft.Extensions.Options;

namespace Canvas.Server.Documents;

public sealed class DocumentHostOptions
{
    public const string Section = "Documents";

    /// <summary>Ops appended after the last snapshot before the server folds them into a new one.</summary>
    public int OpsPerSnapshot { get; set; } = 200;

    /// <summary>How long a document stays loaded without requests.</summary>
    public TimeSpan IdleTimeout { get; set; } = TimeSpan.FromMinutes(10);
}

/// <summary>
/// Keeps the documents in use loaded in visualcore, one at a time per document, so every op is checked
/// against the document before it reaches the log. The log stays the source of truth: a loaded document
/// catches up with ops other writers appended, and is thrown away whenever it might have drifted.
/// </summary>
public sealed class DocumentHost : IDisposable
{
    private const int MaxAppendAttempts = 3;

    private readonly ConcurrentDictionary<Guid, Lazy<Task<LiveDocument?>>> documents = new();
    private readonly DocumentStore store;
    private readonly TimeProvider clock;
    private readonly DocumentHostOptions options;
    private readonly ILogger<DocumentHost> logger;
    private readonly ITimer evictionTimer;

    public DocumentHost(DocumentStore store, TimeProvider clock, IOptions<DocumentHostOptions> options, ILogger<DocumentHost> logger)
    {
        this.store = store;
        this.clock = clock;
        this.options = options.Value;
        this.logger = logger;
        var period = this.options.IdleTimeout / 4;
        evictionTimer = clock.CreateTimer(_ => EvictIdle(), null, period, period);
    }

    /// <summary>
    /// Checks <paramref name="ops"/> against the document in order and logs the valid ones. Ops already in
    /// the log keep their sequence number. Returns null if the document doesn't exist.
    /// </summary>
    /// <exception cref="ArgumentException">An op is malformed. Nothing was logged.</exception>
    public async Task<AppendOpsResponse?> AppendAsync(Guid id, IReadOnlyList<OpInput> ops, CancellationToken ct)
    {
        for (var attempt = 1; ; attempt++)
        {
            var (found, response) = await WithDocumentAsync(id, live => AppendOnceAsync(live, ops, ct), ct);
            if (!found)
            {
                return null;
            }
            if (response is not null)
            {
                return response;
            }
            if (attempt == MaxAppendAttempts)
            {
                throw new InvalidOperationException($"Document {id} kept changing while ops were appended.");
            }
        }
    }

    /// <summary>Unloads the document, for example after it was deleted.</summary>
    public async Task CloseAsync(Guid id)
    {
        if (!documents.TryGetValue(id, out var entry) || await TryGetLoadedAsync(entry) is not { } live)
        {
            return;
        }
        await live.Gate.WaitAsync();
        try
        {
            Discard(live);
        }
        finally
        {
            live.Gate.Release();
        }
    }

    public void Dispose()
    {
        evictionTimer.Dispose();
        foreach (var entry in documents.Values)
        {
            if (entry.IsValueCreated && entry.Value.IsCompletedSuccessfully && entry.Value.Result is { } live)
            {
                live.Core.Dispose();
            }
        }
        documents.Clear();
    }

    /// <summary>Null asks the caller to try again on a freshly loaded document.</summary>
    private async Task<AppendOpsResponse?> AppendOnceAsync(LiveDocument live, IReadOnlyList<OpInput> ops, CancellationToken ct)
    {
        if (!await CatchUpAsync(live, ct))
        {
            Discard(live);
            return null;
        }

        var logged = await store.FindOpsAsync(live.Id, ops.Select(o => o.Id).Distinct().ToList(), ct);
        var fresh = ops.Where(o => !logged.ContainsKey(o.Id)).DistinctBy(o => o.Id).ToList();
        var refusals = live.Core.Apply(fresh.Select(o => new VisualCoreOp(o.Kind, o.Data)).ToList());
        var accepted = fresh.Where((_, i) => refusals[i] is null).ToList();
        var errors = fresh.Select((op, i) => (op.Id, Error: refusals[i]))
            .Where(r => r.Error is not null)
            .ToDictionary(r => r.Id, r => r.Error!);

        if (await store.AppendAsync(live.Id, live.Seq, accepted, ct) != AppendStatus.Appended)
        {
            Discard(live);
            return null;
        }

        var seqs = new Dictionary<Guid, long>(logged);
        foreach (var op in accepted)
        {
            seqs[op.Id] = ++live.Seq;
        }
        await CompactIfDueAsync(live, ct);

        var acks = ops.Select(o => new OpAck(o.Id, seqs.TryGetValue(o.Id, out var seq) ? seq : null, errors.GetValueOrDefault(o.Id))).ToList();
        return new AppendOpsResponse(acks, live.Seq);
    }

    /// <summary>Applies ops other writers logged since the document was loaded. False if the document is gone.</summary>
    private async Task<bool> CatchUpAsync(LiveDocument live, CancellationToken ct)
    {
        var head = await store.GetHeadAsync(live.Id, ct);
        if (head is null)
        {
            return false;
        }
        if (head.HeadSeq == live.Seq)
        {
            return true;
        }
        if (head.SnapshotSeq > live.Seq)
        {
            var reloaded = await LoadAsync(live.Id, ct);
            if (reloaded is null)
            {
                return false;
            }
            live.Core.Dispose();
            (live.Core, live.Seq, live.SnapshotSeq) = (reloaded.Core, reloaded.Seq, reloaded.SnapshotSeq);
            return true;
        }
        await ReplayAsync(live, ct);
        return true;
    }

    private async Task CompactIfDueAsync(LiveDocument live, CancellationToken ct)
    {
        if (live.Seq - live.SnapshotSeq < options.OpsPerSnapshot)
        {
            return;
        }
        var status = await store.SaveSnapshotAsync(live.Id, live.Seq, live.Core.Snapshot().GetRawText(), ct);
        if (status == SnapshotSaveStatus.Saved)
        {
            live.SnapshotSeq = live.Seq;
        }
        else
        {
            logger.LogWarning("Snapshot of document {Id} at seq {Seq} not saved: {Status}", live.Id, live.Seq, status);
        }
    }

    private async Task<(bool Found, T Value)> WithDocumentAsync<T>(Guid id, Func<LiveDocument, Task<T>> work, CancellationToken ct)
    {
        while (true)
        {
            var live = await OpenAsync(id);
            if (live is null)
            {
                return (false, default!);
            }

            await live.Gate.WaitAsync(ct);
            try
            {
                if (live.Closed)
                {
                    continue;
                }
                live.LastUsed = clock.GetTimestamp();
                try
                {
                    return (true, await work(live));
                }
                catch
                {
                    Discard(live);
                    throw;
                }
            }
            finally
            {
                live.Gate.Release();
            }
        }
    }

    private async Task<LiveDocument?> OpenAsync(Guid id)
    {
        Lazy<Task<LiveDocument?>> created = null!;
        created = new Lazy<Task<LiveDocument?>>(() => LoadEntryAsync(id, created));
        var entry = documents.GetOrAdd(id, created);
        try
        {
            if (await entry.Value is { } live)
            {
                return live;
            }
        }
        catch
        {
            documents.TryRemove(KeyValuePair.Create(id, entry));
            throw;
        }
        documents.TryRemove(KeyValuePair.Create(id, entry));
        return null;
    }

    private async Task<LiveDocument?> LoadEntryAsync(Guid id, Lazy<Task<LiveDocument?>> entry)
    {
        var live = await LoadAsync(id, CancellationToken.None);
        if (live is not null)
        {
            live.Entry = entry;
        }
        return live;
    }

    private async Task<LiveDocument?> LoadAsync(Guid id, CancellationToken ct)
    {
        var detail = await store.GetAsync(id, ct);
        if (detail is null)
        {
            return null;
        }

        var core = VisualCoreDocument.Open(detail.Snapshot?.Data.GetRawText());
        if (core.RejectedOnLoad > 0)
        {
            logger.LogWarning("Document {Id}: {Count} nodes of its snapshot were refused, the first at {Path}: {Message}",
                id, core.RejectedOnLoad, core.FirstRejection?.Path, core.FirstRejection?.Message);
        }
        var seq = detail.Snapshot?.Seq ?? 0;
        var live = new LiveDocument(id, core, seq, seq) { LastUsed = clock.GetTimestamp() };
        try
        {
            await ReplayAsync(live, ct);
        }
        catch
        {
            core.Dispose();
            throw;
        }
        return live;
    }

    /// <summary>Applies the log after the document's seq. Ops it refuses are skipped, as when the editor loads.</summary>
    private async Task ReplayAsync(LiveDocument live, CancellationToken ct)
    {
        while (true)
        {
            var result = await store.GetOpsAsync(live.Id, live.Seq, DocumentStore.MaxOpsPerPage, ct);
            if (result.Status != OpsQueryStatus.Ok)
            {
                throw new InvalidOperationException($"Document {live.Id} changed while its log was read: {result.Status}.");
            }
            var page = result.Page!;
            if (page.Ops.Count == 0)
            {
                return;
            }

            var ops = page.Ops.Select(o => new VisualCoreOp(o.Kind, o.Data)).ToList();
            OpRefusal?[] refusals;
            try
            {
                refusals = live.Core.Apply(ops);
            }
            catch (ArgumentException)
            {
                refusals = ops.Select(op => TryApply(live.Core, op)).ToArray();
            }
            var refused = refusals.Where(r => r is not null).ToList();
            if (refused.Count > 0)
            {
                logger.LogWarning("Document {Id}: {Count} logged ops were refused on replay, the first at {Path}: {Message}",
                    live.Id, refused.Count, refused[0]!.Path, refused[0]!.Message);
            }

            live.Seq = page.Ops[^1].Seq;
            if (live.Seq >= page.HeadSeq)
            {
                return;
            }
        }
    }

    private static OpRefusal? TryApply(VisualCoreDocument core, VisualCoreOp op)
    {
        try
        {
            return core.Apply([op])[0];
        }
        catch (ArgumentException malformed)
        {
            return new OpRefusal("", malformed.Message);
        }
    }

    private void EvictIdle()
    {
        var now = clock.GetTimestamp();
        foreach (var entry in documents.Values)
        {
            if (!entry.IsValueCreated || !entry.Value.IsCompletedSuccessfully || entry.Value.Result is not { } live)
            {
                continue;
            }
            if (clock.GetElapsedTime(live.LastUsed, now) < options.IdleTimeout || !live.Gate.Wait(0))
            {
                continue;
            }
            try
            {
                Discard(live);
            }
            finally
            {
                live.Gate.Release();
            }
        }
    }

    private static async Task<LiveDocument?> TryGetLoadedAsync(Lazy<Task<LiveDocument?>> entry)
    {
        if (!entry.IsValueCreated)
        {
            return null;
        }
        try
        {
            return await entry.Value;
        }
        catch
        {
            return null;
        }
    }

    /// <summary>Unloads a document whose gate the caller holds.</summary>
    private void Discard(LiveDocument live)
    {
        if (live.Closed)
        {
            return;
        }
        live.Closed = true;
        if (live.Entry is not null)
        {
            documents.TryRemove(KeyValuePair.Create(live.Id, live.Entry));
        }
        live.Core.Dispose();
    }

    private sealed class LiveDocument(Guid id, VisualCoreDocument core, long seq, long snapshotSeq)
    {
        public Guid Id { get; } = id;
        public SemaphoreSlim Gate { get; } = new(1, 1);
        public VisualCoreDocument Core { get; set; } = core;
        public long Seq { get; set; } = seq;
        public long SnapshotSeq { get; set; } = snapshotSeq;
        public long LastUsed { get; set; }
        public bool Closed { get; set; }
        public Lazy<Task<LiveDocument?>>? Entry { get; set; }
    }
}
