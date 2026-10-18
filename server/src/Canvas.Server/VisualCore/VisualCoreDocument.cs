using System.Text;
using System.Text.Json;

namespace Canvas.Server.VisualCore;

/// <summary>An op as the log stores it: its kind and payload.</summary>
public readonly record struct VisualCoreOp(string Kind, JsonElement Data);

/// <summary>
/// A document loaded into visualcore, which validates ops and answers queries the same way the editor
/// does. Not thread-safe: callers serialize every call, reads included.
/// </summary>
public sealed class VisualCoreDocument : IDisposable
{
    private readonly DocumentHandle handle;

    private VisualCoreDocument(DocumentHandle handle, int rejectedOnLoad)
    {
        this.handle = handle;
        RejectedOnLoad = rejectedOnLoad;
    }

    /// <summary>Nodes of the snapshot the document refused when it was opened.</summary>
    public int RejectedOnLoad { get; }

    /// <summary>Opens a snapshot, or an empty document when <paramref name="snapshot"/> is null.</summary>
    /// <exception cref="InvalidDataException">The snapshot is malformed.</exception>
    public static VisualCoreDocument Open(string? snapshot)
    {
        var handle = NativeMethods.Open(snapshot, out var rejected);
        if (handle.IsInvalid)
        {
            handle.Dispose();
            throw new InvalidDataException("visualcore rejected the snapshot.");
        }
        return new VisualCoreDocument(handle, rejected);
    }

    /// <summary>Applies <paramref name="ops"/> in order. Returns, per op, whether it was valid; invalid ops change nothing.</summary>
    /// <exception cref="ArgumentException">An op's kind is unknown or its payload malformed. Nothing was applied.</exception>
    public bool[] Apply(IReadOnlyList<VisualCoreOp> ops)
    {
        var result = NativeMethods.Take(NativeMethods.Apply(handle, Serialize(ops)))
            ?? throw new ArgumentException("visualcore could not read the ops.", nameof(ops));
        return JsonSerializer.Deserialize<bool[]>(result)!;
    }

    public JsonElement Snapshot() => Parse(NativeMethods.Snapshot(handle))!.Value;

    /// <summary>Layer rows, topmost first, each followed by its children.</summary>
    public JsonElement Layers() => Parse(NativeMethods.Layers(handle))!.Value;

    /// <summary>A node with its laid-out box in <paramref name="mode"/>, or null for an unknown node or mode.</summary>
    public JsonElement? Node(Guid id, Guid? mode = null) =>
        Parse(NativeMethods.Node(handle, id.ToString("D"), mode?.ToString("D")));

    /// <summary>Tokens with values resolved in <paramref name="mode"/>, or null for an unknown mode.</summary>
    public JsonElement? Tokens(Guid? mode = null) => Parse(NativeMethods.Tokens(handle, mode?.ToString("D")));

    public JsonElement Modes() => Parse(NativeMethods.Modes(handle))!.Value;

    public void Dispose() => handle.Dispose();

    private static string Serialize(IReadOnlyList<VisualCoreOp> ops)
    {
        using var buffer = new MemoryStream();
        using (var writer = new Utf8JsonWriter(buffer))
        {
            writer.WriteStartArray();
            foreach (var op in ops)
            {
                writer.WriteStartObject();
                writer.WriteString("kind", op.Kind);
                writer.WritePropertyName("data");
                op.Data.WriteTo(writer);
                writer.WriteEndObject();
            }
            writer.WriteEndArray();
        }
        return Encoding.UTF8.GetString(buffer.GetBuffer(), 0, (int)buffer.Length);
    }

    private static JsonElement? Parse(nint text)
    {
        if (NativeMethods.Take(text) is not { } json)
        {
            return null;
        }
        using var document = JsonDocument.Parse(json);
        return document.RootElement.Clone();
    }
}
