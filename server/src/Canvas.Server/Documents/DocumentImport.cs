using System.Text.Json;
using Canvas.Server.VisualCore;

namespace Canvas.Server.Documents;

/// <summary>The outcome of importing a snapshot: the new document, or why nothing was imported.</summary>
public sealed record ImportResult(DocumentSummary? Document, string? Problem, OpRefusal? Reason);

public static class DocumentImport
{
    public const int MaxSnapshotBytes = 2 * 1024 * 1024;

    /// <summary>Validates a snapshot with visualcore, then creates a separate document atomically.</summary>
    public static async Task<ImportResult> ImportAsync(DocumentStore store, string name, JsonElement snapshot, CancellationToken ct)
    {
        if (snapshot.ValueKind != JsonValueKind.Object ||
            System.Text.Encoding.UTF8.GetByteCount(snapshot.GetRawText()) > MaxSnapshotBytes)
        {
            return new ImportResult(null, "Provide a Canvas snapshot of at most 2 MB.", null);
        }
        try
        {
            using var core = VisualCoreDocument.Open(snapshot.GetRawText());
            if (core.RejectedOnLoad != 0)
            {
                return new ImportResult(null,
                    "The snapshot has invalid layers, tokens or dependencies. Nothing was imported.", core.FirstRejection);
            }
            var document = await store.CreateAsync(name, ct, core.Snapshot().GetRawText());
            return new ImportResult(document, null, null);
        }
        catch (SnapshotRejectedException rejected)
        {
            return new ImportResult(null, "visualcore could not read this snapshot. Nothing was imported.", rejected.Reason);
        }
    }
}
