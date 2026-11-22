using System.Collections.Concurrent;
using System.ComponentModel.DataAnnotations;

namespace Canvas.Server.Documents;

/// <summary>What an editor reports about its selection.</summary>
/// <param name="EditorId">Chosen by the editor instance, so a later report or its close replaces only its own.</param>
/// <param name="Ids">Ids of the selected nodes, in selection order. Empty when nothing is selected.</param>
public sealed record SelectionReport(Guid EditorId, [Required, MaxLength(EditorSelections.MaxIds)] IReadOnlyList<Guid> Ids);

public sealed record EditorSelection(Guid EditorId, IReadOnlyList<Guid> Ids, DateTimeOffset ReportedAt);

/// <summary>
/// The selection of the editor that reported last for each document, in memory. A report that is not
/// renewed within <see cref="Lifetime"/> counts as a disconnected editor.
/// </summary>
public sealed class EditorSelections(TimeProvider clock)
{
    public const int MaxIds = 1000;

    public static readonly TimeSpan Lifetime = TimeSpan.FromSeconds(30);

    private readonly ConcurrentDictionary<Guid, EditorSelection> selections = new();

    public void Report(Guid documentId, Guid editorId, IReadOnlyList<Guid> ids) =>
        selections[documentId] = new EditorSelection(editorId, ids, clock.GetUtcNow());

    /// <summary>Forgets the selection if <paramref name="editorId"/> still holds it.</summary>
    public void Clear(Guid documentId, Guid editorId)
    {
        if (selections.TryGetValue(documentId, out var current) && current.EditorId == editorId)
        {
            selections.TryRemove(new KeyValuePair<Guid, EditorSelection>(documentId, current));
        }
    }

    public void Forget(Guid documentId) => selections.TryRemove(documentId, out _);

    /// <summary>The live selection, or null when no editor is connected.</summary>
    public EditorSelection? Current(Guid documentId)
    {
        if (!selections.TryGetValue(documentId, out var current))
        {
            return null;
        }
        if (clock.GetUtcNow() - current.ReportedAt > Lifetime)
        {
            selections.TryRemove(new KeyValuePair<Guid, EditorSelection>(documentId, current));
            return null;
        }
        return current;
    }
}
