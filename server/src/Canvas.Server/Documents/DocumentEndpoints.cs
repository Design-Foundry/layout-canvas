using System.Text.Json;
using Microsoft.AspNetCore.Http.HttpResults;

namespace Canvas.Server.Documents;

public static class DocumentEndpoints
{
    public static RouteGroupBuilder MapDocuments(this IEndpointRouteBuilder routes)
    {
        var documents = routes.MapGroup("/documents").WithTags("Documents");

        documents.MapGet("/", ListDocuments).WithName(nameof(ListDocuments));
        documents.MapPost("/", CreateDocument).WithName(nameof(CreateDocument)).ProducesValidationProblem();
        documents.MapGet("/{id:guid}", GetDocument).WithName(nameof(GetDocument));
        documents.MapPatch("/{id:guid}", RenameDocument).WithName(nameof(RenameDocument)).ProducesValidationProblem();
        documents.MapDelete("/{id:guid}", DeleteDocument).WithName(nameof(DeleteDocument));
        documents.MapPost("/{id:guid}/ops", AppendOps).WithName(nameof(AppendOps));
        documents.MapGet("/{id:guid}/ops", GetOps).WithName(nameof(GetOps)).ProducesProblem(StatusCodes.Status410Gone);
        documents.MapPut("/{id:guid}/snapshot", SaveSnapshot).WithName(nameof(SaveSnapshot))
            .ProducesProblem(StatusCodes.Status400BadRequest)
            .ProducesProblem(StatusCodes.Status409Conflict);

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

    private static async Task<Results<Ok<DocumentSummary>, NotFound>> RenameDocument(
        Guid id, RenameDocumentRequest request, DocumentStore store, CancellationToken ct) =>
        await store.RenameAsync(id, request.Name, ct) is { } document ? TypedResults.Ok(document) : TypedResults.NotFound();

    private static async Task<Results<NoContent, NotFound>> DeleteDocument(Guid id, DocumentStore store, CancellationToken ct) =>
        await store.DeleteAsync(id, ct) ? TypedResults.NoContent() : TypedResults.NotFound();

    private static async Task<Results<Ok<AppendOpsResponse>, NotFound, ValidationProblem>> AppendOps(
        Guid id, AppendOpsRequest request, DocumentStore store, CancellationToken ct)
    {
        if (request.Ops.Any(op => op.Data.ValueKind == JsonValueKind.Undefined))
        {
            return TypedResults.ValidationProblem(new Dictionary<string, string[]>
            {
                ["ops"] = ["Every op needs a data payload."],
            });
        }

        return await store.AppendOpsAsync(id, request.Ops, ct) is { } response
            ? TypedResults.Ok(response)
            : TypedResults.NotFound();
    }

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

    private static async Task<Results<NoContent, NotFound, ProblemHttpResult>> SaveSnapshot(
        Guid id, SaveSnapshotRequest request, DocumentStore store, CancellationToken ct)
    {
        if (request.Data.ValueKind == JsonValueKind.Undefined)
        {
            return TypedResults.Problem(statusCode: StatusCodes.Status400BadRequest, title: "Snapshot data is required.");
        }

        return await store.SaveSnapshotAsync(id, request.Seq, request.Data, ct) switch
        {
            SnapshotSaveStatus.Saved => TypedResults.NoContent(),
            SnapshotSaveStatus.NotFound => TypedResults.NotFound(),
            SnapshotSaveStatus.NotNewer => TypedResults.Problem(
                statusCode: StatusCodes.Status409Conflict, title: "A snapshot at or after this seq already exists."),
            _ => TypedResults.Problem(
                statusCode: StatusCodes.Status409Conflict, title: "Snapshot seq is beyond the head of the op log."),
        };
    }
}
