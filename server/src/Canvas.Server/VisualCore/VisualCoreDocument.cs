using System.Text;
using System.Text.Json;
using Canvas.Server.Documents;

namespace Canvas.Server.VisualCore;

/// <summary>visualcore could not read a snapshot.</summary>
public sealed class SnapshotRejectedException(OpRefusal reason)
    : Exception(reason.Path.Length == 0 ? reason.Message : $"{reason.Path}: {reason.Message}")
{
    public OpRefusal Reason { get; } = reason;
}

/// <summary>An op as the log stores it: its kind and payload.</summary>
public readonly record struct VisualCoreOp(string Kind, JsonElement Data);

/// <summary>
/// A document loaded into visualcore, which validates ops and answers queries the same way the editor
/// does. Not thread-safe: callers serialize every call, reads included.
/// </summary>
public sealed class VisualCoreDocument : IDisposable
{
    private readonly DocumentHandle handle;

    private VisualCoreDocument(DocumentHandle handle, int rejectedOnLoad, OpRefusal? firstRejection)
    {
        this.handle = handle;
        RejectedOnLoad = rejectedOnLoad;
        FirstRejection = firstRejection;
    }

    /// <summary>Nodes of the snapshot the document refused when it was opened.</summary>
    public int RejectedOnLoad { get; }

    /// <summary>Why the document refused the first of <see cref="RejectedOnLoad"/>, if any.</summary>
    public OpRefusal? FirstRejection { get; }

    /// <summary>Opens a snapshot, or an empty document when <paramref name="snapshot"/> is null.</summary>
    /// <exception cref="SnapshotRejectedException">The snapshot is malformed.</exception>
    public static VisualCoreDocument Open(string? snapshot)
    {
        var handle = NativeMethods.Open(snapshot, out var rejected);
        var reason = LastError();
        if (handle.IsInvalid)
        {
            handle.Dispose();
            throw new SnapshotRejectedException(reason ?? new OpRefusal("", "the snapshot is malformed"));
        }
        return new VisualCoreDocument(handle, rejected, reason);
    }

    /// <summary>Applies <paramref name="ops"/> in order. Returns, per op, null when it applied or why it was refused; refused ops change nothing.</summary>
    /// <exception cref="ArgumentException">An op's kind is unknown or its payload malformed. Nothing was applied.</exception>
    public OpRefusal?[] Apply(IReadOnlyList<VisualCoreOp> ops)
    {
        var text = NativeMethods.Apply(handle, Serialize(ops));
        var reason = text == 0 ? LastError() : null;
        var result = NativeMethods.Take(text)
            ?? throw new ArgumentException(reason is null ? "visualcore could not read the ops." : $"{reason.Path}: {reason.Message}", nameof(ops));
        using var document = JsonDocument.Parse(result);
        return document.RootElement.EnumerateArray()
            .Select(entry => entry.ValueKind == JsonValueKind.True ? null : entry.Deserialize<OpRefusal>(JsonOptions))
            .ToArray();
    }

    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);

    private static OpRefusal? LastError() =>
        NativeMethods.Take(NativeMethods.LastError()) is { } json ? JsonSerializer.Deserialize<OpRefusal>(json, JsonOptions) : null;

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
