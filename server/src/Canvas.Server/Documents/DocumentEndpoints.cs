using System.Text.Json;
using Canvas.Server.VisualCore;
using Microsoft.AspNetCore.Http.HttpResults;

namespace Canvas.Server.Documents;

public static class DocumentEndpoints
{
    public static RouteGroupBuilder MapDocuments(this IEndpointRouteBuilder routes)
    {
        var documents = routes.MapGroup("/documents").WithTags("Documents");

        documents.MapGet("/", ListDocuments).WithName(nameof(ListDocuments));
        documents.MapPost("/", CreateDocument).WithName(nameof(CreateDocument)).ProducesValidationProblem();
        documents.MapPost("/import", ImportDocument).WithName(nameof(ImportDocument)).ProducesValidationProblem()
            .WithMetadata(new Microsoft.AspNetCore.Mvc.RequestSizeLimitAttribute(3 * 1024 * 1024));
        documents.MapGet("/{id:guid}", GetDocument).WithName(nameof(GetDocument));
        documents.MapPatch("/{id:guid}", RenameDocument).WithName(nameof(RenameDocument)).ProducesValidationProblem();
        documents.MapDelete("/{id:guid}", DeleteDocument).WithName(nameof(DeleteDocument));
        documents.MapPost("/{id:guid}/ops", AppendOps).WithName(nameof(AppendOps));
        documents.MapGet("/{id:guid}/ops", GetOps).WithName(nameof(GetOps)).ProducesProblem(StatusCodes.Status410Gone);
        documents.MapPut("/{id:guid}/selection", PutSelection).WithName(nameof(PutSelection)).ProducesValidationProblem();
        documents.MapDelete("/{id:guid}/selection", DeleteSelection).WithName(nameof(DeleteSelection));
        documents.MapGet("/{id:guid}/render-requests", GetRenderRequest).WithName(nameof(GetRenderRequest));
        documents.MapPut("/{id:guid}/render-requests/{requestId:guid}/result", PutRenderResult).WithName(nameof(PutRenderResult)).ProducesValidationProblem();

        return documents;
    }

    private static async Task<Ok<IReadOnlyList<DocumentSummary>>> ListDocuments(DocumentStore store, CancellationToken ct) =>
        TypedResults.Ok(await store.ListAsync(ct));

    private static async Task<Created<DocumentSummary>> CreateDocument(CreateDocumentRequest request, DocumentStore store, CancellationToken ct)
    {
        var document = await store.CreateAsync(request.Name, ct);
        return TypedResults.Created($"/api/documents/{document.Id}", document);
    }

    private static async Task<Results<Ok<DocumentDetail>, NotFound>> GetDocument(Guid id, DocumentStore store, CancellationToken ct) =>
        await store.GetAsync(id, ct) is { } document ? TypedResults.Ok(document) : TypedResults.NotFound();

    /// <summary>Validates an imported snapshot with visualcore, then creates a separate document atomically.</summary>
    private static async Task<Results<Created<DocumentSummary>, ValidationProblem>> ImportDocument(
        ImportDocumentRequest request, DocumentStore store, CancellationToken ct)
    {
        var result = await DocumentImport.ImportAsync(store, request.Name, request.Snapshot, ct);
        return result.Document is { } document
            ? TypedResults.Created($"/api/documents/{document.Id}", document)
            : ImportProblem(result.Problem!, result.Reason);
    }

    private static ValidationProblem ImportProblem(string message, OpRefusal? reason = null)
    {
        var errors = new Dictionary<string, string[]> { ["snapshot"] = [message] };
        if (reason is not null)
        {
            errors[reason.Path.Length == 0 ? "snapshot" : $"snapshot.{reason.Path}"] =
                reason.Path.Length == 0 ? [message, reason.Message] : [reason.Message];
        }
        return TypedResults.ValidationProblem(errors);
    }

    private static async Task<Results<Ok<DocumentSummary>, NotFound>> RenameDocument(
        Guid id, RenameDocumentRequest request, DocumentStore store, CancellationToken ct) =>
        await store.RenameAsync(id, request.Name, ct) is { } document ? TypedResults.Ok(document) : TypedResults.NotFound();

    private static async Task<Results<NoContent, NotFound>> DeleteDocument(
        Guid id, DocumentStore store, DocumentHost host, EditorSelections selections, CancellationToken ct)
    {
        if (!await store.DeleteAsync(id, ct))
        {
            return TypedResults.NotFound();
        }
        selections.Forget(id);
        await host.CloseAsync(id);
        return TypedResults.NoContent();
    }

    /// <summary>Records what an editor has selected. It lapses unless renewed within 30 seconds.</summary>
    private static async Task<Results<NoContent, NotFound>> PutSelection(
        Guid id, SelectionReport report, DocumentStore store, EditorSelections selections, CancellationToken ct)
    {
        if (await store.GetHeadAsync(id, ct) is null)
        {
            return TypedResults.NotFound();
        }
        selections.Report(id, report.EditorId, report.Ids);
        return TypedResults.NoContent();
    }

    /// <summary>Drops the selection an editor reported, when it closes. Does nothing if another editor reported since.</summary>
    private static NoContent DeleteSelection(Guid id, Guid editorId, EditorSelections selections, RenderRequests renders)
    {
        selections.Clear(id, editorId);
        renders.EditorGone(id, editorId);
        return TypedResults.NoContent();
    }

    /// <summary>Long-polls for a node the live editor should render. No content when none came within <paramref name="wait"/> seconds.</summary>
    private static async Task<Results<Ok<RenderRequest>, NoContent>> GetRenderRequest(
        Guid id, Guid editorId, RenderRequests renders, CancellationToken ct, int wait = 20) =>
        await renders.ClaimAsync(id, editorId, TimeSpan.FromSeconds(Math.Clamp(wait, 0, 25)), ct) is { } request
            ? TypedResults.Ok(request)
            : TypedResults.NoContent();

    /// <summary>Delivers a rendered PNG, or the reason it failed, for a request this editor claimed.</summary>
    private static Results<NoContent, NotFound, ValidationProblem> PutRenderResult(
        Guid id, Guid requestId, RenderResult result, RenderRequests renders)
    {
        RenderedImage? image = null;
        if (result.Error is null)
        {
            try
            {
                image = new RenderedImage(Convert.FromBase64String(result.Png ?? ""), result.Width, result.Height, result.Scale);
            }
            catch (FormatException)
            {
                return TypedResults.ValidationProblem(new Dictionary<string, string[]> { ["png"] = ["Not valid base64."] });
            }
            if (image.Png.Length == 0 || image.Width is < 1 or > RenderRequests.MaxSize || image.Height is < 1 or > RenderRequests.MaxSize)
            {
                return TypedResults.ValidationProblem(new Dictionary<string, string[]> { ["png"] = ["The image is empty or larger than the limit."] });
            }
        }
        return renders.Complete(id, requestId, result.EditorId, image, result.Error) ? TypedResults.NoContent() : TypedResults.NotFound();
    }

    /// <summary>Logs the ops the document accepts. Refused ops come back without a sequence number.</summary>
    private static async Task<Results<Ok<AppendOpsResponse>, NotFound, ValidationProblem>> AppendOps(
        Guid id, AppendOpsRequest request, DocumentHost host, CancellationToken ct)
    {
        if (request.Ops.Any(op => op.Data.ValueKind == JsonValueKind.Undefined))
        {
            return OpsProblem("Every op needs a data payload.");
        }

        try
        {
            return await host.AppendAsync(id, request.Ops, OpActors.Human, ct) is { } response
                ? TypedResults.Ok(response)
                : TypedResults.NotFound();
        }
        catch (ArgumentException)
        {
            return OpsProblem("An op has an unknown kind or a malformed payload.");
        }
    }

    private static ValidationProblem OpsProblem(string message) =>
        TypedResults.ValidationProblem(new Dictionary<string, string[]> { ["ops"] = [message] });

    /// <param name="after">Return ops with a sequence number greater than this.</param>
    /// <param name="limit">Page size, at most 5000.</param>
    private static async Task<Results<Ok<OpsPage>, NotFound, ProblemHttpResult>> GetOps(
        Guid id, DocumentStore store, CancellationToken ct, long after = 0, int limit = 1000)
    {
        var result = await store.GetOpsAsync(id, after, limit, ct);
        return result.Status switch
        {
            OpsQueryStatus.Ok => TypedResults.Ok(result.Page),
            OpsQueryStatus.NotFound => TypedResults.NotFound(),
            _ => TypedResults.Problem(
                statusCode: StatusCodes.Status410Gone,
                title: "Ops compacted",
                detail: $"Ops up to seq {result.SnapshotSeq} are folded into the snapshot. Reload the document."),
        };
    }
}
