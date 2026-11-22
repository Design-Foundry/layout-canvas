using System.ComponentModel.DataAnnotations;

namespace Canvas.Server.Documents;

/// <summary>A node an agent wants to see, handed to the editor that has the document open.</summary>
/// <param name="MaxSize">Longest side, in pixels, the image may have. The editor lowers the scale to fit.</param>
public sealed record RenderRequest(Guid Id, Guid NodeId, double Scale, int MaxSize);

/// <summary>What an editor sends back: a PNG (base64), its size and the scale it used, or the reason it could not render.</summary>
public sealed record RenderResult(
    Guid EditorId,
    [MaxLength(RenderRequests.MaxPngBase64)] string? Png,
    int Width,
    int Height,
    double Scale,
    [MaxLength(500)] string? Error);

public sealed record RenderedImage(byte[] Png, int Width, int Height, double Scale);

/// <summary>A render that cannot be delivered. The message says what the caller can do about it.</summary>
public sealed class RenderException(string message) : Exception(message);

/// <summary>
/// Render requests waiting for the editor that is live for a document, in memory. An editor long-polls
/// <see cref="ClaimAsync"/>; only the live editor (see <see cref="EditorSelections"/>) is handed a request,
/// and only one editor ever holds it.
/// </summary>
public sealed class RenderRequests(EditorSelections selections)
{
    public const int MaxPngBase64 = 24 * 1024 * 1024;
    public const int MaxSize = 2048;
    /// <summary>How long a request waits for its image.</summary>
    public TimeSpan Timeout { get; set; } = TimeSpan.FromSeconds(10);

    public const string NoEditor = "No editor has this document open. Open the document in the editor, then try again.";

    private sealed class Pending(RenderRequest request)
    {
        public RenderRequest Request { get; } = request;
        public Guid? ClaimedBy { get; set; }
        public TaskCompletionSource<RenderedImage> Completion { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);
    }

    private sealed class Queue
    {
        public List<Pending> Items { get; } = [];
        public TaskCompletionSource Signal { get; set; } = NewSignal();
    }

    private readonly Dictionary<Guid, Queue> documents = new();

    private static TaskCompletionSource NewSignal() => new(TaskCreationOptions.RunContinuationsAsynchronously);

    /// <summary>Asks the live editor to render a node and waits for the image.</summary>
    public async Task<RenderedImage> RequestAsync(Guid documentId, Guid nodeId, double scale, CancellationToken ct)
    {
        if (selections.Current(documentId) is null)
        {
            throw new RenderException(NoEditor);
        }
        var pending = new Pending(new RenderRequest(Guid.NewGuid(), nodeId, scale, MaxSize));
        lock (documents)
        {
            if (!documents.TryGetValue(documentId, out var queue))
            {
                documents[documentId] = queue = new();
            }
            queue.Items.Add(pending);
            Wake(queue);
        }
        try
        {
            return await pending.Completion.Task.WaitAsync(Timeout, ct);
        }
        catch (TimeoutException)
        {
            throw new RenderException(pending.ClaimedBy is null
                ? "The editor did not pick up the render request in time. Check that the document is open and visible in the editor."
                : "The editor took too long to render the node. Try a smaller scale or a smaller node.");
        }
        finally
        {
            lock (documents)
            {
                if (documents.TryGetValue(documentId, out var queue) && queue.Items.Remove(pending) && queue.Items.Count == 0)
                {
                    documents.Remove(documentId);
                }
            }
        }
    }

    /// <summary>
    /// Waits up to <paramref name="wait"/> for a request <paramref name="editorId"/> should render. Null when none
    /// came, or when another editor is the live one.
    /// </summary>
    public async Task<RenderRequest?> ClaimAsync(Guid documentId, Guid editorId, TimeSpan wait, CancellationToken ct)
    {
        var deadline = Environment.TickCount64 + (long)wait.TotalMilliseconds;
        while (true)
        {
            Task signal;
            lock (documents)
            {
                var queue = documents.GetValueOrDefault(documentId);
                if (queue is not null && selections.Current(documentId)?.EditorId == editorId
                    && queue.Items.FirstOrDefault(p => p.ClaimedBy is null) is { } next)
                {
                    next.ClaimedBy = editorId;
                    return next.Request;
                }
                signal = (queue ??= documents[documentId] = new()).Signal.Task;
            }
            var remaining = deadline - Environment.TickCount64;
            if (remaining <= 0)
            {
                Prune(documentId);
                return null;
            }
            try
            {
                await signal.WaitAsync(TimeSpan.FromMilliseconds(remaining), ct);
            }
            catch (TimeoutException)
            {
                Prune(documentId);
                return null;
            }
            catch (OperationCanceledException)
            {
                Prune(documentId);
                return null;
            }
        }
    }

    /// <summary>Delivers the outcome of a request. False when it is unknown, finished or claimed by another editor.</summary>
    public bool Complete(Guid documentId, Guid requestId, Guid editorId, RenderedImage? image, string? error)
    {
        Pending? pending;
        lock (documents)
        {
            pending = documents.GetValueOrDefault(documentId)?.Items.FirstOrDefault(p => p.Request.Id == requestId && p.ClaimedBy == editorId);
        }
        if (pending is null)
        {
            return false;
        }
        if (image is not null && error is null)
        {
            return pending.Completion.TrySetResult(image);
        }
        return pending.Completion.TrySetException(new RenderException($"The editor could not render the node: {error ?? "no image returned"}"));
    }

    /// <summary>
    /// An editor closed. What it was rendering fails now, and so do requests nobody else can take.
    /// Call after its selection is cleared.
    /// </summary>
    public void EditorGone(Guid documentId, Guid editorId)
    {
        var orphaned = selections.Current(documentId) is null;
        List<Pending> failing;
        lock (documents)
        {
            failing = documents.GetValueOrDefault(documentId)?.Items
                .Where(p => p.ClaimedBy == editorId || (orphaned && p.ClaimedBy is null)).ToList() ?? [];
        }
        foreach (var pending in failing)
        {
            pending.Completion.TrySetException(new RenderException(pending.ClaimedBy is null
                ? NoEditor
                : "The editor was closed before it finished rendering. Open the document in the editor and try again."));
        }
    }

    private void Prune(Guid documentId)
    {
        lock (documents)
        {
            if (documents.TryGetValue(documentId, out var queue) && queue.Items.Count == 0)
            {
                documents.Remove(documentId);
            }
        }
    }

    private static void Wake(Queue queue)
    {
        var previous = queue.Signal;
        queue.Signal = NewSignal();
        previous.TrySetResult();
    }
}
