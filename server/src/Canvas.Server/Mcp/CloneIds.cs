using Canvas.Server.VisualCore;

namespace Canvas.Server.Mcp;

/// <summary>The ids <c>CloneNode</c> gives the copies of a subtree, derived by visualcore.</summary>
public static class CloneIds
{
    /// <summary>The id of the copy of <paramref name="node"/> under the clone root <paramref name="scope"/>.</summary>
    public static Guid Derive(Guid scope, Guid node) =>
        Guid.Parse(NativeMethods.Take(NativeMethods.DerivedId(scope.ToString(), node.ToString())) ?? throw new InvalidOperationException("visualcore did not derive an id"));

    /// <summary>Source id to copy id for every node of <paramref name="existing"/> that has a copy under <paramref name="root"/>, the clone's source included.</summary>
    public static Dictionary<Guid, Guid> Copies(Guid source, Guid root, IEnumerable<Guid> existing)
    {
        var present = existing.ToHashSet();
        var copies = new Dictionary<Guid, Guid> { [source] = root };
        foreach (var candidate in present)
        {
            var copy = Derive(root, candidate);
            if (present.Contains(copy))
            {
                copies[candidate] = copy;
            }
        }
        return copies;
    }
}
