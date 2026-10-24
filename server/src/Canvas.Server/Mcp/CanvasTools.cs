using System.ComponentModel;
using System.Text.Json;
using Canvas.Server.Documents;
using Canvas.Server.VisualCore;
using ModelContextProtocol;
using ModelContextProtocol.Server;

namespace Canvas.Server.Mcp;

/// <summary>Tools that read and edit Canvas documents through the same op log the editor syncs with.</summary>
[McpServerToolType]
public sealed class CanvasTools(DocumentStore store, DocumentHost host)
{
    private static readonly Lazy<string> FormatGuide = new(() =>
    {
        using var stream = typeof(CanvasTools).Assembly.GetManifestResourceStream("Canvas.Server.Mcp.FormatGuide.md")!;
        using var reader = new StreamReader(stream);
        return reader.ReadToEnd();
    });

    [McpServerTool(Name = "get_format_guide", ReadOnly = true, Idempotent = true)]
    [Description("How Canvas documents are shaped: nodes, auto layout, text, paths, tokens, modes, components and every op kind apply_ops takes. Read it before writing ops.")]
    public string GetFormatGuide() => FormatGuide.Value;

    [McpServerTool(Name = "list_documents", ReadOnly = true, Idempotent = true)]
    [Description("Lists Canvas documents, most recently edited first.")]
    public Task<IReadOnlyList<DocumentSummary>> ListDocuments(CancellationToken ct) => store.ListAsync(ct);

    [McpServerTool(Name = "create_document")]
    [Description("Creates an empty document and returns its id.")]
    public Task<DocumentSummary> CreateDocument([Description("Document name, up to 200 characters.")] string name, CancellationToken ct) =>
        store.CreateAsync(name, ct);

    [McpServerTool(Name = "import_document")]
    [Description("Creates a document from a whole snapshot ({ version, modes, tokens, nodes }, at most 2 MB). Nothing is created if any part is invalid; the error names where and why.")]
    public async Task<DocumentSummary> ImportDocument(
        [Description("Document name, up to 200 characters.")] string name,
        [Description("The snapshot, in the shape the format guide describes.")] JsonElement snapshot,
        CancellationToken ct)
    {
        var result = await DocumentImport.ImportAsync(store, name, snapshot, ct);
        return result.Document ?? throw new McpException(Describe(result.Problem!, result.Reason));
    }

    [McpServerTool(Name = "get_document", ReadOnly = true, Idempotent = true)]
    [Description("A document's modes, its tokens with values resolved in a mode, and its layer tree in document order (each layer followed by its children, with depth; siblings back to front, as auto layout places them).")]
    public async Task<JsonElement> GetDocument(
        [Description("Document id.")] Guid documentId,
        [Description("Mode to resolve token values in; the default mode when omitted.")] Guid? mode = null,
        CancellationToken ct = default)
    {
        var summary = (await store.ListAsync(ct)).FirstOrDefault(d => d.Id == documentId) ?? throw NotFound(documentId);
        var (found, value) = await host.ReadAsync(documentId, core => (Modes: core.Modes(), Tokens: core.Tokens(mode), Layers: core.Layers()), ct);
        if (!found)
        {
            throw NotFound(documentId);
        }
        var tokens = value.Tokens ?? throw new McpException($"Mode {mode} does not exist.");
        return JsonSerializer.SerializeToElement(new
        {
            summary.Id,
            summary.Name,
            value.Modes,
            Tokens = tokens.EnumerateArray().Select(token => new
            {
                Id = token.GetProperty("id").GetString(),
                Identifier = token.GetProperty("identifier").GetString(),
                Type = token.GetProperty("type").GetString(),
                Alias = token.TryGetProperty("source", out _),
                Resolved = token.TryGetProperty("resolved", out var resolved) ? resolved : (JsonElement?)null,
            }).ToList(),
            Layers = InDocumentOrder(value.Layers),
        }, JsonOptions);
    }

    [McpServerTool(Name = "get_nodes", ReadOnly = true, Idempotent = true)]
    [Description("Full nodes: every stored property, component definition and values, and box, the laid-out rect relative to the parent.")]
    public async Task<JsonElement> GetNodes(
        [Description("Document id.")] Guid documentId,
        [Description("Ids of the nodes to read, layers inside instances included.")] Guid[] nodeIds,
        [Description("Mode to lay out in; the default mode when omitted.")] Guid? mode = null,
        CancellationToken ct = default)
    {
        var (found, nodes) = await host.ReadAsync(documentId,
            core => nodeIds.Select(id => core.Node(id, mode) ?? JsonSerializer.SerializeToElement(new { id, error = "no such node" })).ToList(), ct);
        return found ? JsonSerializer.SerializeToElement(nodes, JsonOptions) : throw NotFound(documentId);
    }

    [McpServerTool(Name = "get_layout", ReadOnly = true, Idempotent = true)]
    [Description("Laid-out boxes in absolute canvas coordinates for a subtree or the whole document, in document order like get_document. overflows marks a layer that extends outside its parent; check it after building to catch clipped text and misaligned children.")]
    public async Task<JsonElement> GetLayout(
        [Description("Document id.")] Guid documentId,
        [Description("Root of the subtree; the whole document when omitted.")] Guid? rootId = null,
        [Description("Mode to lay out in; the default mode when omitted.")] Guid? mode = null,
        CancellationToken ct = default)
    {
        var (found, rows) = await host.ReadAsync(documentId, core => Layout(core, rootId, mode), ct);
        if (!found)
        {
            throw NotFound(documentId);
        }
        return rows is null ? throw new McpException($"Node {rootId} does not exist.") : JsonSerializer.SerializeToElement(rows, JsonOptions);
    }

    [McpServerTool(Name = "apply_ops")]
    [Description("""
        Applies ops in order through the document's log, the same one the editor syncs with; open editors show them after a reload. Any id may be a placeholder such as "$card" (in values or object keys): each placeholder becomes one new UUID across the batch, returned in ids. A CreateNode without order goes after its existing siblings. If any op is malformed (unknown kind, wrong shape, or a value out of range such as letterSpacing below 0), nothing is applied and the error names that op. Otherwise ops the document refuses (a missing parent, a property the node type does not take, a broken component rule) change nothing while the rest still apply; results give each one's error with path and message. Up to 1000 ops per call. See get_format_guide for every op kind.
        """)]
    public async Task<JsonElement> ApplyOps(
        [Description("Document id.")] Guid documentId,
        [Description("Ops as { kind, data }, for example { \"kind\": \"CreateNode\", \"data\": { \"node\": { \"id\": \"$card\", \"type\": \"frame\", ... } } }.")] AgentOp[] ops,
        CancellationToken ct)
    {
        if (ops.Length is 0 or > AppendOpsRequest.MaxOps)
        {
            throw new McpException($"Send between 1 and {AppendOpsRequest.MaxOps} ops.");
        }
        var (found, existing) = await host.ReadAsync(documentId, core => (LastOrders: LastOrders(core), ComponentNames: ComponentNames(core)), ct);
        if (!found)
        {
            throw NotFound(documentId);
        }
        var (prepared, ids) = OpBatch.Prepare(ops, existing.LastOrders, existing.ComponentNames);
        AppendOpsResponse? response;
        try
        {
            response = await host.AppendAsync(documentId, prepared, ct);
        }
        catch (MalformedOpsException malformed)
        {
            throw new McpException($"Nothing was applied: {malformed.Message}");
        }
        if (response is null)
        {
            throw NotFound(documentId);
        }
        var results = response.Acks.Select((ack, i) => new { Index = i, prepared[i].Kind, Applied = ack.Seq is not null, ack.Error }).ToList();
        return JsonSerializer.SerializeToElement(new
        {
            Applied = results.Count(r => r.Applied),
            Refused = results.Count(r => !r.Applied),
            Ids = ids,
            Results = results,
        }, JsonOptions);
    }

    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web)
    {
        DefaultIgnoreCondition = System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingNull,
    };

    private static McpException NotFound(Guid id) => new($"Document {id} does not exist.");

    private static string Describe(string problem, OpRefusal? reason) =>
        reason is null ? problem : $"{problem} {(reason.Path.Length == 0 ? "" : reason.Path + ": ")}{reason.Message}";

    private static Dictionary<Guid, string> LastOrders(VisualCoreDocument core)
    {
        var last = new Dictionary<Guid, string>();
        foreach (var row in core.Layers().EnumerateArray())
        {
            if (row.TryGetProperty("inInstance", out _) || core.Node(row.GetProperty("id").GetGuid()) is not { } node)
            {
                continue;
            }
            var parent = node.TryGetProperty("parent", out var p) ? p.GetGuid() : Guid.Empty;
            var order = node.GetProperty("order").GetString()!;
            if (string.CompareOrdinal(order, last.GetValueOrDefault(parent)) > 0)
            {
                last[parent] = order;
            }
        }
        return last;
    }

    private static Dictionary<Guid, string> ComponentNames(VisualCoreDocument core) =>
        core.Layers().EnumerateArray()
            .Where(row => row.GetProperty("type").GetString() == "component" && !row.TryGetProperty("inInstance", out _))
            .ToDictionary(row => row.GetProperty("id").GetGuid(), row => row.GetProperty("name").GetString() ?? "");

    private sealed record LayerTree(JsonElement Row, List<LayerTree> Children);

    /// <summary>Reorders layer rows, which come topmost first, so siblings run back to front.</summary>
    private static List<JsonElement> InDocumentOrder(JsonElement layers)
    {
        var roots = new List<LayerTree>();
        var path = new List<LayerTree>();
        foreach (var row in layers.EnumerateArray())
        {
            var depth = row.GetProperty("depth").GetInt32();
            var tree = new LayerTree(row, []);
            if (path.Count > depth)
            {
                path.RemoveRange(depth, path.Count - depth);
            }
            (depth == 0 || path.Count < depth ? roots : path[depth - 1].Children).Add(tree);
            path.Add(tree);
        }
        var ordered = new List<JsonElement>();
        void Walk(List<LayerTree> siblings)
        {
            for (var i = siblings.Count - 1; i >= 0; i--)
            {
                ordered.Add(siblings[i].Row);
                Walk(siblings[i].Children);
            }
        }
        Walk(roots);
        return ordered;
    }

    private sealed record LayoutRow(Guid Id, string Name, string Type, int Depth, double X, double Y, double W, double H, bool? Overflows);

    /// <summary>Null when <paramref name="rootId"/> is not in the document.</summary>
    private static List<LayoutRow>? Layout(VisualCoreDocument core, Guid? rootId, Guid? mode)
    {
        var rows = new List<LayoutRow>();
        var ancestors = new List<LayoutRow>();
        int? rootDepth = null;
        foreach (var row in InDocumentOrder(core.Layers()))
        {
            var id = row.GetProperty("id").GetGuid();
            var depth = row.GetProperty("depth").GetInt32();
            if (ancestors.Count > depth)
            {
                ancestors.RemoveRange(depth, ancestors.Count - depth);
            }
            var box = core.Node(id, mode)?.GetProperty("box");
            var parent = depth > 0 && ancestors.Count >= depth ? ancestors[depth - 1] : null;
            var x = (box?.GetProperty("x").GetDouble() ?? 0) + (parent?.X ?? 0);
            var y = (box?.GetProperty("y").GetDouble() ?? 0) + (parent?.Y ?? 0);
            var w = box?.GetProperty("w").GetDouble() ?? 0;
            var h = box?.GetProperty("h").GetDouble() ?? 0;
            const double Slack = 0.5;
            var overflows = parent is not null &&
                (x < parent.X - Slack || y < parent.Y - Slack || x + w > parent.X + parent.W + Slack || y + h > parent.Y + parent.H + Slack);
            var laid = new LayoutRow(id, row.GetProperty("name").GetString() ?? "", row.GetProperty("type").GetString()!, depth,
                Math.Round(x, 2), Math.Round(y, 2), Math.Round(w, 2), Math.Round(h, 2), overflows ? true : null);
            ancestors.Add(laid);

            if (rootId is null || id == rootId)
            {
                rootDepth ??= rootId is null ? 0 : depth;
            }
            else if (rootDepth is { } top && depth <= top)
            {
                break;
            }
            if (rootDepth is not null)
            {
                rows.Add(laid);
            }
        }
        return rootDepth is null && rootId is not null ? null : rows;
    }
}
