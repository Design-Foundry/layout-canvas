namespace Canvas.Server.Mcp;

/// <summary>The ids <c>CloneNode</c> gives the copies of a subtree, derived the way visualcore derives them.</summary>
public static class CloneIds
{
    /// <summary>The id of the copy of <paramref name="node"/> under the clone root <paramref name="scope"/>.</summary>
    public static Guid Derive(Guid scope, Guid node)
    {
        var (scopeHi, scopeLo) = Split(scope);
        var (nodeHi, nodeLo) = Split(node);
        var hi = Mix(scopeHi ^ Mix(unchecked(nodeHi + 0xA0761D6478BD642FUL)));
        var lo = Mix(scopeLo ^ Mix(unchecked(nodeLo + 0xE7037ED1A0B428DBUL)));
        hi = (hi & ~0xF000UL) | 0x4000UL;
        lo = (lo & ~(0xC000UL << 48)) | (0x8000UL << 48);
        var text = $"{hi:x16}{lo:x16}";
        return Guid.Parse($"{text[..8]}-{text[8..12]}-{text[12..16]}-{text[16..20]}-{text[20..]}");
    }

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

    private static (ulong Hi, ulong Lo) Split(Guid id)
    {
        var text = id.ToString("N");
        return (Convert.ToUInt64(text[..16], 16), Convert.ToUInt64(text[16..], 16));
    }

    private static ulong Mix(ulong x)
    {
        unchecked
        {
            x ^= x >> 30;
            x *= 0xBF58476D1CE4E5B9UL;
            x ^= x >> 27;
            x *= 0x94D049BB133111EBUL;
            x ^= x >> 31;
            return x;
        }
    }
}
