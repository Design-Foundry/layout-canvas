using System.Net;
using System.Net.Http.Json;
using System.Text.Json;
using Canvas.Server.Documents;

namespace Canvas.Server.Tests;

public sealed class DocumentEndpointsTests(CanvasFactory factory) : IClassFixture<CanvasFactory>
{
    private readonly HttpClient client = factory.CreateClient();
    private static CancellationToken Ct => TestContext.Current.CancellationToken;

    [Fact]
    public async Task Created_document_is_listed_and_opens_empty()
    {
        var created = await CreateAsync("Landing page");

        var list = await client.GetFromJsonAsync<List<DocumentSummary>>("/api/documents", Ct);
        var detail = await client.GetFromJsonAsync<DocumentDetail>($"/api/documents/{created.Id}", Ct);

        Assert.Contains(list!, d => d.Id == created.Id && d.Name == "Landing page");
        Assert.Equal(0, detail!.HeadSeq);
        Assert.Null(detail.Snapshot);
    }

    [Fact]
    public async Task Blank_name_is_rejected()
    {
        var response = await client.PostAsJsonAsync("/api/documents", new CreateDocumentRequest(""), Ct);

        Assert.Equal(HttpStatusCode.BadRequest, response.StatusCode);
    }

    [Fact]
    public async Task Rename_updates_the_name()
    {
        var created = await CreateAsync("Draft");

        var response = await client.PatchAsJsonAsync($"/api/documents/{created.Id}", new RenameDocumentRequest("Checkout"), Ct);
        var renamed = await response.Content.ReadFromJsonAsync<DocumentSummary>(Ct);

        Assert.Equal("Checkout", renamed!.Name);
    }

    [Fact]
    public async Task Deleted_document_is_gone()
    {
        var created = await CreateAsync("Scratch");

        var delete = await client.DeleteAsync($"/api/documents/{created.Id}", Ct);
        var get = await client.GetAsync($"/api/documents/{created.Id}", Ct);

        Assert.Equal(HttpStatusCode.NoContent, delete.StatusCode);
        Assert.Equal(HttpStatusCode.NotFound, get.StatusCode);
    }

    [Fact]
    public async Task Ops_get_consecutive_sequence_numbers()
    {
        var document = await CreateAsync("Ops");

        var first = await AppendAsync(document.Id, Op("CreateNode"), Op("SetProp"));
        var second = await AppendAsync(document.Id, Op("DeleteNode"));

        Assert.Equal([1L, 2L], first.Acks.Select(a => a.Seq));
        Assert.Equal(3, second.Acks.Single().Seq);
        Assert.Equal(3, second.HeadSeq);
    }

    [Fact]
    public async Task Resent_op_keeps_its_original_sequence_number()
    {
        var document = await CreateAsync("Retry");
        var op = Op("CreateNode");

        await AppendAsync(document.Id, op);
        var retry = await AppendAsync(document.Id, op, Op("SetProp"));

        Assert.Equal([1L, 2L], retry.Acks.Select(a => a.Seq));
        Assert.Equal(2, retry.HeadSeq);
    }

    [Fact]
    public async Task Ops_are_read_back_in_order_after_a_sequence_number()
    {
        var document = await CreateAsync("Read");
        await AppendAsync(document.Id, Op("A"), Op("B"), Op("C"));

        var page = await client.GetFromJsonAsync<OpsPage>($"/api/documents/{document.Id}/ops?after=1", Ct);

        Assert.Equal(["B", "C"], page!.Ops.Select(o => o.Kind));
        Assert.Equal(3, page.HeadSeq);
    }

    [Fact]
    public async Task Snapshot_compacts_the_log_but_keeps_retries_idempotent()
    {
        var document = await CreateAsync("Compaction");
        var early = Op("CreateNode");
        await AppendAsync(document.Id, early, Op("SetProp"), Op("SetProp"));

        var save = await client.PutAsJsonAsync($"/api/documents/{document.Id}/snapshot",
            new SaveSnapshotRequest(2, Json("""{"nodes":[]}""")), Ct);
        var detail = await client.GetFromJsonAsync<DocumentDetail>($"/api/documents/{document.Id}", Ct);
        var stale = await client.GetAsync($"/api/documents/{document.Id}/ops?after=0", Ct);
        var tail = await client.GetFromJsonAsync<OpsPage>($"/api/documents/{document.Id}/ops?after=2", Ct);
        var retry = await AppendAsync(document.Id, early);

        Assert.Equal(HttpStatusCode.NoContent, save.StatusCode);
        Assert.Equal(2, detail!.Snapshot!.Seq);
        Assert.Equal(HttpStatusCode.Gone, stale.StatusCode);
        Assert.Equal([3L], tail!.Ops.Select(o => o.Seq));
        Assert.Equal(1, retry.Acks.Single().Seq);
        Assert.Equal(3, retry.HeadSeq);
    }

    [Fact]
    public async Task Snapshot_beyond_the_head_is_rejected()
    {
        var document = await CreateAsync("Ahead");
        await AppendAsync(document.Id, Op("CreateNode"));

        var response = await client.PutAsJsonAsync($"/api/documents/{document.Id}/snapshot",
            new SaveSnapshotRequest(5, Json("{}")), Ct);

        Assert.Equal(HttpStatusCode.Conflict, response.StatusCode);
    }

    [Fact]
    public async Task Ops_for_a_missing_document_are_not_found()
    {
        var response = await client.PostAsJsonAsync($"/api/documents/{Guid.NewGuid()}/ops",
            new AppendOpsRequest([Op("CreateNode")]), Ct);

        Assert.Equal(HttpStatusCode.NotFound, response.StatusCode);
    }

    private async Task<DocumentSummary> CreateAsync(string name)
    {
        var response = await client.PostAsJsonAsync("/api/documents", new CreateDocumentRequest(name), Ct);
        response.EnsureSuccessStatusCode();
        return (await response.Content.ReadFromJsonAsync<DocumentSummary>(Ct))!;
    }

    private async Task<AppendOpsResponse> AppendAsync(Guid documentId, params OpInput[] ops)
    {
        var response = await client.PostAsJsonAsync($"/api/documents/{documentId}/ops", new AppendOpsRequest(ops), Ct);
        response.EnsureSuccessStatusCode();
        return (await response.Content.ReadFromJsonAsync<AppendOpsResponse>(Ct))!;
    }

    private static OpInput Op(string kind) => new(Guid.NewGuid(), kind, Json("""{"node":"n1"}"""));

    private static JsonElement Json(string json) => JsonDocument.Parse(json).RootElement.Clone();
}
