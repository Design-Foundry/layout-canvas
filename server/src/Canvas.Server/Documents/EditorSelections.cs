using System.Collections.Concurrent;
using System.ComponentModel.DataAnnotations;

namespace Canvas.Server.Documents;

/// <summary>What an editor reports about its selection.</summary>
/// <param name="EditorId">Chosen by the editor instance, so a later report or its close replaces only its own.</param>
/// <param name="Ids">Ids of the selected nodes, in selection order. Empty when nothing is selected.</param>
public sealed record SelectionReport(Guid EditorId, [Required, MaxLength(EditorSelections.MaxIds)] IReadOnlyList<Guid> Ids);

public sealed record EditorSelection(Guid EditorId, IReadOnlyList<Guid> Ids, DateTimeOffset ReportedAt);

/// <summary>
/// The selection of each editor that reported for a document, in memory. The live selection is the one
/// that changed last; repeating an unchanged selection only keeps its editor connected. A report that is
/// not renewed within <see cref="Lifetime"/> counts as a disconnected editor.
/// </summary>
public sealed class EditorSelections(TimeProvider clock)
{
    public const int MaxIds = 1000;

    public static readonly TimeSpan Lifetime = TimeSpan.FromSeconds(30);

    private sealed record Entry(IReadOnlyList<Guid> Ids, long Change, DateTimeOffset ChangedAt, DateTimeOffset RenewedAt);

    private long changes;

    private readonly Dictionary<Guid, Dictionary<Guid, Entry>> documents = new();

    public void Report(Guid documentId, Guid editorId, IReadOnlyList<Guid> ids)
    {
        var now = clock.GetUtcNow();
        lock (documents)
        {
            if (!documents.TryGetValue(documentId, out var editors))
            {
                documents[documentId] = editors = new();
            }
            Prune(editors, now);
            editors[editorId] = editors.TryGetValue(editorId, out var previous) && previous.Ids.SequenceEqual(ids)
                ? previous with { RenewedAt = now }
                : new Entry(ids, ++changes, now, now);
        }
    }

    /// <summary>Forgets the selection <paramref name="editorId"/> reported. Other editors keep theirs.</summary>
    public void Clear(Guid documentId, Guid editorId)
    {
        lock (documents)
        {
            if (documents.TryGetValue(documentId, out var editors) && editors.Remove(editorId) && editors.Count == 0)
            {
                documents.Remove(documentId);
            }
        }
    }

    public void Forget(Guid documentId)
    {
        lock (documents)
        {
            documents.Remove(documentId);
        }
    }

    /// <summary>The live selection, or null when no editor is connected.</summary>
    public EditorSelection? Current(Guid documentId)
    {
        var now = clock.GetUtcNow();
        lock (documents)
        {
            if (!documents.TryGetValue(documentId, out var editors))
            {
                return null;
            }
            Prune(editors, now);
            if (editors.Count == 0)
            {
                documents.Remove(documentId);
                return null;
            }
            var (editorId, entry) = editors.MaxBy(e => e.Value.Change);
            return new EditorSelection(editorId, entry.Ids, entry.ChangedAt);
        }
    }

    private static void Prune(Dictionary<Guid, Entry> editors, DateTimeOffset now)
    {
        foreach (var editorId in editors.Where(e => now - e.Value.RenewedAt > Lifetime).Select(e => e.Key).ToList())
        {
            editors.Remove(editorId);
        }
    }
}
