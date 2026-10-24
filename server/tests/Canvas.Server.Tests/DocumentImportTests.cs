using System.Net;
using System.Net.Http.Json;
using System.Text.Json;
using Canvas.Server.Documents;

namespace Canvas.Server.Tests;

public sealed class DocumentImportTests(CanvasFactory factory) : IClassFixture<CanvasFactory>
{
    private readonly HttpClient client = factory.CreateClient();
    private static CancellationToken Ct => TestContext.Current.CancellationToken;

    [Fact]
    public async Task Imported_component_survives_reload_and_accepts_later_edits()
    {
        var id = Guid.NewGuid();
        var snapshot = JsonSerializer.SerializeToElement(new
        {
            version = 2, modes = Array.Empty<object>(), tokens = Array.Empty<object>(),
            nodes = new[] { new { id, type = "component", order = "a0", name = "Button", w = 120, h = 40 } },
        });
        var response = await client.PostAsJsonAsync("/api/documents/import", new ImportDocumentRequest("Button", snapshot), Ct);
        Assert.Equal(HttpStatusCode.Created, response.StatusCode);
        var created = (await response.Content.ReadFromJsonAsync<DocumentSummary>(Ct))!;
        var detail = (await client.GetFromJsonAsync<DocumentDetail>($"/api/documents/{created.Id}", Ct))!;
        Assert.Equal(1, detail.HeadSeq);
        Assert.Equal(1, detail.Snapshot!.Seq);
        Assert.Equal(id.ToString(), detail.Snapshot.Data.GetProperty("nodes")[0].GetProperty("id").GetString());

        var edit = new OpInput(Guid.NewGuid(), "SetProps", JsonSerializer.SerializeToElement(new { id, patch = new { name = "Updated" } }));
        var appended = await client.PostAsJsonAsync($"/api/documents/{created.Id}/ops", new AppendOpsRequest([edit]), Ct);
        var ack = (await appended.Content.ReadFromJsonAsync<AppendOpsResponse>(Ct))!;
        Assert.Equal(2, ack.Acks.Single().Seq);
    }

    [Fact]
    public async Task Invalid_dependency_creates_no_partial_document()
    {
        var before = (await client.GetFromJsonAsync<List<DocumentSummary>>("/api/documents", Ct))!.Count;
        var snapshot = JsonSerializer.SerializeToElement(new
        {
            version = 2, modes = Array.Empty<object>(), tokens = Array.Empty<object>(),
            nodes = new[] { new { id = Guid.NewGuid(), type = "instance", order = "a0", component = Guid.NewGuid() } },
        });
        var response = await client.PostAsJsonAsync("/api/documents/import", new ImportDocumentRequest("Invalid", snapshot), Ct);
        Assert.Equal(HttpStatusCode.BadRequest, response.StatusCode);
        Assert.Equal(before, (await client.GetFromJsonAsync<List<DocumentSummary>>("/api/documents", Ct))!.Count);
    }

    [Theory]
    [InlineData("null")]
    [InlineData("{}")]
    [InlineData("{\"version\":999,\"nodes\":[]}")]
    public async Task Malformed_snapshot_is_rejected(string json)
    {
        var response = await client.PostAsJsonAsync("/api/documents/import", new ImportDocumentRequest("Invalid", JsonSerializer.Deserialize<JsonElement>(json)), Ct);
        Assert.Equal(HttpStatusCode.BadRequest, response.StatusCode);
    }

    [Fact]
    public async Task Oversized_snapshot_is_refused_before_native_parsing()
    {
        var snapshot = JsonSerializer.SerializeToElement(new { padding = new string('x', 2 * 1024 * 1024) });
        var response = await client.PostAsJsonAsync("/api/documents/import", new ImportDocumentRequest("Too large", snapshot), Ct);
        Assert.Equal(HttpStatusCode.BadRequest, response.StatusCode);
    }
}
