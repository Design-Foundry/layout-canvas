using System.Net;
using System.Net.Http.Json;
using System.Text.Json;
using Canvas.Server.Documents;
using Microsoft.Extensions.DependencyInjection;

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
        var node = Guid.NewGuid();

        var first = await AppendAsync(document.Id, CreateNode(node), Rename(node));
        var second = await AppendAsync(document.Id, DeleteNode(node));

        Assert.Equal([1L, 2L], first.Acks.Select(a => a.Seq));
        Assert.Equal(3, second.Acks.Single().Seq);
        Assert.Equal(3, second.HeadSeq);
    }

    [Fact]
    public async Task Resent_op_keeps_its_original_sequence_number()
    {
        var document = await CreateAsync("Retry");
        var node = Guid.NewGuid();
        var op = CreateNode(node);

        await AppendAsync(document.Id, op);
        var retry = await AppendAsync(document.Id, op, Rename(node));

        Assert.Equal([1L, 2L], retry.Acks.Select(a => a.Seq));
        Assert.Equal(2, retry.HeadSeq);
    }

    [Fact]
    public async Task Ops_are_read_back_in_order_after_a_sequence_number()
    {
        var document = await CreateAsync("Read");
        var node = Guid.NewGuid();
        await AppendAsync(document.Id, CreateNode(node), Rename(node), DeleteNode(node));

        var page = await client.GetFromJsonAsync<OpsPage>($"/api/documents/{document.Id}/ops?after=1", Ct);

        Assert.Equal(["SetProps", "DeleteNode"], page!.Ops.Select(o => o.Kind));
        Assert.Equal(3, page.HeadSeq);
    }

    [Fact]
    public async Task Ops_the_document_refuses_are_not_logged()
    {
        var document = await CreateAsync("Refused");
        var node = Guid.NewGuid();

        var response = await AppendAsync(document.Id, Rename(node), CreateNode(node), CreateNode(Guid.NewGuid(), parent: Guid.NewGuid()));
        var page = await client.GetFromJsonAsync<OpsPage>($"/api/documents/{document.Id}/ops", Ct);

        Assert.Equal([null, 1L, null], response.Acks.Select(a => a.Seq));
        Assert.Equal(1, response.HeadSeq);
        Assert.Equal(["CreateNode"], page!.Ops.Select(o => o.Kind));
    }

    [Fact]
    public async Task Malformed_ops_are_rejected_without_logging_any()
    {
        var document = await CreateAsync("Malformed");

        var response = await client.PostAsJsonAsync($"/api/documents/{document.Id}/ops",
            new AppendOpsRequest([CreateNode(Guid.NewGuid()), new OpInput(Guid.NewGuid(), "SetProp", Json("{}"))]), Ct);
        var detail = await client.GetFromJsonAsync<DocumentDetail>($"/api/documents/{document.Id}", Ct);

        Assert.Equal(HttpStatusCode.BadRequest, response.StatusCode);
        Assert.Equal(0, detail!.HeadSeq);
    }

    [Fact]
    public async Task The_server_compacts_the_log_and_keeps_retries_idempotent()
    {
        var document = await CreateAsync("Compaction");
        var node = Guid.NewGuid();
        var early = CreateNode(node);
        await AppendAsync(document.Id, early, Rename(node), Rename(node), Rename(node));
        await AppendAsync(document.Id, Rename(node));

        var detail = await client.GetFromJsonAsync<DocumentDetail>($"/api/documents/{document.Id}", Ct);
        var stale = await client.GetAsync($"/api/documents/{document.Id}/ops?after=0", Ct);
        var retry = await AppendAsync(document.Id, early);

        Assert.Equal(CanvasFactory.OpsPerSnapshot, detail!.Snapshot!.Seq);
        Assert.Contains(node.ToString(), detail.Snapshot.Data.GetRawText());
        Assert.Equal(HttpStatusCode.Gone, stale.StatusCode);
        Assert.Equal(1, retry.Acks.Single().Seq);
        Assert.Equal(5, retry.HeadSeq);
    }

    [Fact]
    public async Task Ops_from_another_writer_are_seen_before_validating()
    {
        var document = await CreateAsync("Two writers");
        var node = Guid.NewGuid();
        await AppendAsync(document.Id, CreateNode(Guid.NewGuid()));
        var store = factory.Services.GetRequiredService<DocumentStore>();

        var elsewhere = await store.AppendAsync(document.Id, expectedHead: 1, [CreateNode(node)], Ct);
        var stale = await store.AppendAsync(document.Id, expectedHead: 1, [Rename(node)], Ct);
        var response = await AppendAsync(document.Id, Rename(node));

        Assert.Equal(AppendStatus.Appended, elsewhere);
        Assert.Equal(AppendStatus.Conflict, stale);
        Assert.Equal(3, response.Acks.Single().Seq);
    }

    [Fact]
    public async Task Ops_for_a_missing_document_are_not_found()
    {
        var response = await client.PostAsJsonAsync($"/api/documents/{Guid.NewGuid()}/ops",
            new AppendOpsRequest([CreateNode(Guid.NewGuid())]), Ct);

        Assert.Equal(HttpStatusCode.NotFound, response.StatusCode);
    }

    [Fact]
    public async Task Ops_for_a_deleted_document_are_not_found()
    {
        var document = await CreateAsync("Deleted");
        await AppendAsync(document.Id, CreateNode(Guid.NewGuid()));

        await client.DeleteAsync($"/api/documents/{document.Id}", Ct);
        var response = await client.PostAsJsonAsync($"/api/documents/{document.Id}/ops",
            new AppendOpsRequest([CreateNode(Guid.NewGuid())]), Ct);

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

    private static OpInput CreateNode(Guid node, Guid? parent = null) => new(Guid.NewGuid(), "CreateNode", Json(parent is null
        ? $$$"""{"node":{"id":"{{{node}}}","type":"rect","order":"V","w":10,"h":10}}"""
        : $$$"""{"node":{"id":"{{{node}}}","type":"rect","order":"V","w":10,"h":10,"parent":"{{{parent}}}"}}"""));

    private static OpInput Rename(Guid node) =>
        new(Guid.NewGuid(), "SetProps", Json($$$"""{"id":"{{{node}}}","patch":{"name":"Renamed"}}"""));

    private static OpInput DeleteNode(Guid node) => new(Guid.NewGuid(), "DeleteNode", Json($$"""{"id":"{{node}}"}"""));

    private static JsonElement Json(string json) => JsonDocument.Parse(json).RootElement.Clone();
}
