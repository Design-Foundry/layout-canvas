using System.Net;
using System.Net.Http.Json;
using System.Text;
using System.Text.Json;
using Canvas.Server.Documents;
using Canvas.Server.Mcp;
using Canvas.Server.Storage;
using Microsoft.Data.Sqlite;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Hosting.Internal;
using Microsoft.Extensions.Options;
using ModelContextProtocol;

namespace Canvas.Server.Tests;

public sealed class OpChangesTests
{
    private static Change Describe(string kind, string data) =>
        OpChanges.Describe(new OpRecord(1, Guid.NewGuid(), kind, JsonDocument.Parse(data).RootElement.Clone(), DateTimeOffset.UnixEpoch, OpActors.Agent));

    [Fact]
    public void Set_props_lists_fields_and_keeps_small_values_only()
    {
        var node = Guid.NewGuid();
        var small = Describe("SetProps", $$$"""{"id":"{{{node}}}","patch":{"name":"Card","w":120}}""");
        var large = Describe("SetProps", $$$"""{"id":"{{{node}}}","patch":{"name":"{{{new string('x', 400)}}}","w":120}}""");

        Assert.Equal([node], small.NodeIds);
        Assert.Equal(["name", "w"], small.Fields);
        Assert.Equal(120, small.Values!["w"]!.GetValue<int>());
        Assert.Equal(["name", "w"], large.Fields);
        Assert.Null(large.Values);
    }

    [Fact]
    public void Create_move_delete_clone_and_batch_name_their_nodes()
    {
        var (a, b, parent) = (Guid.NewGuid(), Guid.NewGuid(), Guid.NewGuid());
        var created = Describe("CreateNode", $$$"""{"node":{"id":"{{{a}}}","type":"frame","name":"Card","parent":"{{{parent}}}","w":10} }""");
        Assert.Equal([a], created.NodeIds);
        Assert.Equal("frame", created.Values!["type"]!.GetValue<string>());
        Assert.Contains("w", created.Fields!);
        Assert.DoesNotContain("id", created.Fields!);

        Assert.Equal([a], Describe("MoveNode", $$$"""{"id":"{{{a}}}","parent":"{{{parent}}}","order":"a1"}""").NodeIds);
        Assert.Equal([a], Describe("DeleteNode", $$$"""{"id":"{{{a}}}"}""").NodeIds);

        var clone = Describe("CloneNode", $$$"""{"source":"{{{a}}}","id":"{{{b}}}","patches":{"{{{a}}}":{"name":"x"} }}""");
        Assert.Equal([b, a], clone.NodeIds);
        Assert.Equal(["name"], clone.Fields);

        var batch = Describe("Batch", $$$"""{"ops":[{"kind":"CreateNode","data":{"node":{"id":"{{{a}}}","type":"rect"} }},{"kind":"DeleteNode","data":{"id":"{{{b}}}"} }]}""");
        Assert.Equal([a, b], batch.NodeIds);
    }

    [Fact]
    public void Document_level_ops_name_their_entity()
    {
        var token = Guid.NewGuid();
        var change = Describe("CreateToken", $$$"""{"token":{"id":"{{{token}}}","identifier":"ink"} }""");

        Assert.Empty(change.NodeIds);
        Assert.Equal(new ChangeEntity("token", token), change.Entity);
        Assert.Equal("font", Describe("DeleteFont", $$$"""{"id":"{{{token}}}"}""").Entity!.Type);
        Assert.Equal("fontStack", Describe("ReplaceFontStack", $$$"""{"stack":{"id":"{{{token}}}"} }""").Entity!.Type);
    }
}

public sealed class ChangesTests(CanvasFactory factory) : IClassFixture<CanvasFactory>
{
    private readonly HttpClient client = factory.CreateClient();
    private static CancellationToken Ct => TestContext.Current.CancellationToken;
    private CanvasTools Tools => ActivatorUtilities.CreateInstance<CanvasTools>(factory.Services);

    private static AgentOp Op(string kind, object data) => new(kind, JsonSerializer.SerializeToElement(data));

    private async Task<Guid> CreateAsync(string name) => (await Tools.CreateDocument(name, Ct)).Id;

    private async Task<Guid> CreateNodeAsync(Guid document)
    {
        var result = await Tools.ApplyOps(document, [Op("CreateNode", new { node = new { id = "$n", type = "frame", name = "N", w = 10, h = 10 } })], Ct);
        return result.GetProperty("ids").GetProperty("$n").GetGuid();
    }

    private async Task<JsonElement> RestAppendAsync(Guid document, string body)
    {
        var response = await client.PostAsync($"/api/documents/{document}/ops", new StringContent(body, Encoding.UTF8, "application/json"), Ct);
        response.EnsureSuccessStatusCode();
        return await response.Content.ReadFromJsonAsync<JsonElement>(Ct);
    }

    private static string Rename(Guid node, string name = "Renamed", object? actor = null) =>
        JsonSerializer.Serialize(new { ops = new[] { new { id = Guid.NewGuid(), kind = "SetProps", actor, data = new { id = node, patch = new { name } } } } });

    [Fact]
    public async Task Editor_ops_are_human_and_mcp_ops_are_agent_whatever_the_client_claims()
    {
        var document = await CreateAsync("Actors");
        var node = await CreateNodeAsync(document);
        await RestAppendAsync(document, Rename(node, actor: "agent"));
        await Tools.ApplyOps(document, [Op("SetProps", new { id = node, patch = new { name = "By agent" } })], Ct);

        var page = await client.GetFromJsonAsync<OpsPage>($"/api/documents/{document}/ops?after=0", Ct);
        var changes = await Tools.GetChanges(document, 0, null, Ct);

        Assert.Equal([OpActors.Agent, OpActors.Human, OpActors.Agent], page!.Ops.Select(o => o.Actor));
        Assert.Equal([OpActors.Agent, OpActors.Human, OpActors.Agent], changes.GetProperty("changes").EnumerateArray().Select(c => c.GetProperty("actor").GetString()));
    }

    [Fact]
    public async Task Apply_ops_reports_the_head_after_the_batch()
    {
        var document = await CreateAsync("Head");
        var node = await CreateNodeAsync(document);
        var result = await Tools.ApplyOps(document, [Op("SetProps", new { id = node, patch = new { name = "A" } }), Op("SetProps", new { id = node, patch = new { name = "B" } })], Ct);

        Assert.Equal(3, result.GetProperty("headSeq").GetInt64());
        Assert.Equal(3, (await Tools.GetDocument(document, null, Ct)).GetProperty("headSeq").GetInt64());
    }

    [Fact]
    public async Task Changes_page_in_order_and_continue_from_next_since()
    {
        var document = await CreateAsync("Paging");
        var node = await CreateNodeAsync(document);
        await Tools.ApplyOps(document, [Op("SetProps", new { id = node, patch = new { name = "A" } }), Op("SetProps", new { id = node, patch = new { w = 30 } })], Ct);
        await RestAppendAsync(document, Rename(node, "From editor"));

        var first = await Tools.GetChanges(document, 0, 2, Ct);
        Assert.False(first.GetProperty("compacted").GetBoolean());
        Assert.True(first.GetProperty("hasMore").GetBoolean());
        Assert.Equal(2, first.GetProperty("nextSince").GetInt64());
        Assert.Equal(4, first.GetProperty("headSeq").GetInt64());
        Assert.Equal([1L, 2L], first.GetProperty("changes").EnumerateArray().Select(c => c.GetProperty("seq").GetInt64()));
        Assert.Equal("CreateNode", first.GetProperty("changes")[0].GetProperty("kind").GetString());
        Assert.Equal(node, first.GetProperty("changes")[1].GetProperty("nodeIds")[0].GetGuid());

        var rest = await Tools.GetChanges(document, 2, 2, Ct);
        Assert.False(rest.GetProperty("hasMore").GetBoolean());
        var last = rest.GetProperty("changes")[1];
        Assert.Equal(4, last.GetProperty("seq").GetInt64());
        Assert.Equal("human", last.GetProperty("actor").GetString());
        Assert.Equal("From editor", last.GetProperty("values").GetProperty("name").GetString());

        var caughtUp = await Tools.GetChanges(document, 4, null, Ct);
        Assert.Equal(0, caughtUp.GetProperty("changes").GetArrayLength());
        Assert.False(caughtUp.GetProperty("hasMore").GetBoolean());

        await Assert.ThrowsAsync<McpException>(() => Tools.GetChanges(document, 9, null, Ct));
        await Assert.ThrowsAsync<McpException>(() => Tools.GetChanges(document, -1, null, Ct));
        await Assert.ThrowsAsync<McpException>(() => Tools.GetChanges(Guid.NewGuid(), 0, null, Ct));
    }

    [Fact]
    public async Task Compacted_history_is_reported_and_surviving_ops_keep_their_actor()
    {
        var document = await CreateAsync("Compaction");
        var node = await CreateNodeAsync(document);
        await RestAppendAsync(document, Rename(node, "1"));
        await Tools.ApplyOps(document, Enumerable.Range(0, 3).Select(i => Op("SetProps", new { id = node, patch = new { name = $"a{i}" } })).ToArray(), Ct);
        Assert.Equal(CanvasFactory.OpsPerSnapshot, (await Tools.GetChanges(document, 0, null, Ct)).GetProperty("headSeq").GetInt64());

        var behindFirst = await Tools.GetChanges(document, 0, null, Ct);
        Assert.False(behindFirst.GetProperty("compacted").GetBoolean());

        await RestAppendAsync(document, Rename(node, "2"));
        for (var i = 0; i < 4; i++)
        {
            await Tools.ApplyOps(document, [Op("SetProps", new { id = node, patch = new { name = $"b{i}" } })], Ct);
        }

        var stale = await Tools.GetChanges(document, 0, null, Ct);
        Assert.True(stale.GetProperty("compacted").GetBoolean());
        Assert.Equal(0, stale.GetProperty("changes").GetArrayLength());
        Assert.Contains("get_document", stale.GetProperty("message").GetString());
        Assert.Equal(10, stale.GetProperty("headSeq").GetInt64());

        var recent = await Tools.GetChanges(document, 5, null, Ct);
        Assert.False(recent.GetProperty("compacted").GetBoolean());
        Assert.Equal(5, recent.GetProperty("changes").GetArrayLength());
        Assert.Equal(["human", "agent", "agent", "agent", "agent"], recent.GetProperty("changes").EnumerateArray().Select(c => c.GetProperty("actor").GetString()));

        var database = factory.Services.GetRequiredService<Database>();
        await using var connection = await database.OpenAsync(Ct);
        await using var command = connection.CreateCommand();
        command.CommandText = "SELECT actor FROM ops WHERE document_id = $id AND data IS NULL ORDER BY seq";
        command.Parameters.AddWithValue("$id", document.ToString("D"));
        var kept = new List<string>();
        await using var reader = await command.ExecuteReaderAsync(Ct);
        while (await reader.ReadAsync(Ct))
        {
            kept.Add(reader.GetString(0));
        }
        Assert.Equal(["agent", "human", "agent", "agent", "agent"], kept);
    }

    [Fact]
    public async Task An_imported_document_asks_for_a_reread()
    {
        var document = (await Tools.ImportDocument("Imported", JsonSerializer.SerializeToElement(new { version = 5, modes = Array.Empty<object>(), tokens = Array.Empty<object>(), nodes = Array.Empty<object>() }), Ct)).Id;

        var result = await Tools.GetChanges(document, 0, null, Ct);

        Assert.True(result.GetProperty("compacted").GetBoolean());
        Assert.Equal(1, result.GetProperty("headSeq").GetInt64());
        Assert.Equal(0, (await Tools.GetChanges(document, 1, null, Ct)).GetProperty("changes").GetArrayLength());
    }

    [Fact]
    public async Task A_log_written_before_actors_existed_reads_as_unknown()
    {
        var path = Path.Combine(Path.GetTempPath(), $"canvas-legacy-{Guid.NewGuid():N}.db");
        try
        {
            var document = Guid.NewGuid().ToString("D");
            await using (var connection = new SqliteConnection($"Data Source={path}"))
            {
                await connection.OpenAsync(Ct);
                await using var command = connection.CreateCommand();
                command.CommandText = """
                    CREATE TABLE documents (id TEXT PRIMARY KEY, name TEXT NOT NULL, created_at TEXT NOT NULL, updated_at TEXT NOT NULL,
                        head_seq INTEGER NOT NULL DEFAULT 0, snapshot_seq INTEGER NOT NULL DEFAULT 0, snapshot TEXT NULL);
                    CREATE TABLE ops (document_id TEXT NOT NULL REFERENCES documents (id) ON DELETE CASCADE, seq INTEGER NOT NULL, op_id TEXT NOT NULL,
                        kind TEXT NOT NULL, data TEXT NULL, created_at TEXT NOT NULL, PRIMARY KEY (document_id, seq), UNIQUE (document_id, op_id));
                    INSERT INTO documents VALUES ($document, 'Old', '2026-01-01T00:00:00.0000000+00:00', '2026-01-01T00:00:00.0000000+00:00', 1, 0, NULL);
                    INSERT INTO ops VALUES ($document, 1, $op, 'DeleteNode', $data, '2026-01-01T00:00:00.0000000+00:00');
                    """;
                command.Parameters.AddWithValue("$document", document);
                command.Parameters.AddWithValue("$op", Guid.NewGuid().ToString("D"));
                command.Parameters.AddWithValue("$data", $$"""{"id":"{{Guid.NewGuid()}}"}""");
                await command.ExecuteNonQueryAsync(Ct);
            }

            var database = new Database(Options.Create(new StorageOptions { Database = path }), new HostingEnvironment { ContentRootPath = Path.GetTempPath() });
            await database.InitializeAsync(Ct);
            await database.InitializeAsync(Ct);
            var store = new DocumentStore(database, TimeProvider.System);
            var appended = await store.AppendAsync(Guid.Parse(document), 1, [new OpInput(Guid.NewGuid(), "DeleteNode", JsonDocument.Parse($$$"""{"id":"{{Guid.NewGuid()}}"}""").RootElement.Clone())], OpActors.Agent, Ct);
            var ops = (await store.GetOpsAsync(Guid.Parse(document), 0, 10, Ct)).Page!.Ops;

            Assert.Equal(AppendStatus.Appended, appended);
            Assert.Equal([OpActors.Unknown, OpActors.Agent], ops.Select(o => o.Actor));
        }
        finally
        {
            SqliteConnection.ClearAllPools();
            foreach (var suffix in new[] { "", "-wal", "-shm" })
            {
                File.Delete(path + suffix);
            }
        }
    }
}

public sealed class SelectionTests(CanvasFactory factory) : IClassFixture<CanvasFactory>
{
    private readonly HttpClient client = factory.CreateClient();
    private static CancellationToken Ct => TestContext.Current.CancellationToken;
    private CanvasTools Tools => ActivatorUtilities.CreateInstance<CanvasTools>(factory.Services);

    private static AgentOp Op(string kind, object data) => new(kind, JsonSerializer.SerializeToElement(data));

    private Task<HttpResponseMessage> Report(Guid document, Guid editor, params Guid[] ids) =>
        client.PutAsJsonAsync($"/api/documents/{document}/selection", new SelectionReport(editor, ids), Ct);

    [Fact]
    public async Task Without_an_editor_the_selection_says_so()
    {
        var document = (await Tools.CreateDocument("Empty", Ct)).Id;

        var result = await Tools.GetSelection(document, Ct);

        Assert.False(result.GetProperty("connected").GetBoolean());
        Assert.Equal(0, result.GetProperty("items").GetArrayLength());
        await Assert.ThrowsAsync<McpException>(() => Tools.GetSelection(Guid.NewGuid(), Ct));
    }

    [Fact]
    public async Task The_last_reporting_editor_wins_and_closing_clears_only_its_own()
    {
        var document = (await Tools.CreateDocument("Select", Ct)).Id;
        var made = await Tools.ApplyOps(document,
        [
            Op("CreateNode", new { node = new { id = "$card", type = "frame", name = "Card", w = 100, h = 100 } }),
            Op("CreateNode", new { node = new { id = "$title", type = "frame", name = "Title", parent = "$card", w = 10, h = 10 } }),
        ], Ct);
        var (card, title) = (made.GetProperty("ids").GetProperty("$card").GetGuid(), made.GetProperty("ids").GetProperty("$title").GetGuid());
        var (first, second) = (Guid.NewGuid(), Guid.NewGuid());

        (await Report(document, first, card)).EnsureSuccessStatusCode();
        (await Report(document, second, title)).EnsureSuccessStatusCode();
        var selected = await Tools.GetSelection(document, Ct);

        Assert.True(selected.GetProperty("connected").GetBoolean());
        var item = selected.GetProperty("items").EnumerateArray().Single();
        Assert.Equal(title, item.GetProperty("id").GetGuid());
        Assert.Equal("Title", item.GetProperty("name").GetString());
        Assert.Equal("frame", item.GetProperty("type").GetString());
        Assert.Equal(card, item.GetProperty("frameId").GetGuid());
        Assert.Equal("Card", item.GetProperty("frameName").GetString());

        await client.DeleteAsync($"/api/documents/{document}/selection?editorId={first}", Ct);
        Assert.True((await Tools.GetSelection(document, Ct)).GetProperty("connected").GetBoolean());

        (await Report(document, second, card, Guid.NewGuid())).EnsureSuccessStatusCode();
        var withGhost = await Tools.GetSelection(document, Ct);
        Assert.Equal(1, withGhost.GetProperty("missing").GetArrayLength());
        Assert.False(withGhost.GetProperty("items")[0].TryGetProperty("frameId", out _));

        await client.DeleteAsync($"/api/documents/{document}/selection?editorId={second}", Ct);
        Assert.False((await Tools.GetSelection(document, Ct)).GetProperty("connected").GetBoolean());
    }

    [Fact]
    public async Task An_empty_selection_is_still_a_connected_editor()
    {
        var document = (await Tools.CreateDocument("Nothing", Ct)).Id;

        (await Report(document, Guid.NewGuid())).EnsureSuccessStatusCode();
        var result = await Tools.GetSelection(document, Ct);

        Assert.True(result.GetProperty("connected").GetBoolean());
        Assert.Equal(0, result.GetProperty("items").GetArrayLength());
    }

    [Fact]
    public async Task Reports_for_a_missing_document_are_not_found()
    {
        Assert.Equal(HttpStatusCode.NotFound, (await Report(Guid.NewGuid(), Guid.NewGuid())).StatusCode);
    }

    [Fact]
    public void A_report_that_is_not_renewed_lapses()
    {
        var clock = new ManualClock();
        var selections = new EditorSelections(clock);
        var (document, editor) = (Guid.NewGuid(), Guid.NewGuid());

        selections.Report(document, editor, [Guid.NewGuid()]);
        clock.Advance(EditorSelections.Lifetime - TimeSpan.FromSeconds(1));
        Assert.NotNull(selections.Current(document));
        selections.Report(document, editor, [Guid.NewGuid()]);
        clock.Advance(EditorSelections.Lifetime - TimeSpan.FromSeconds(1));
        Assert.NotNull(selections.Current(document));
        clock.Advance(TimeSpan.FromSeconds(2));
        Assert.Null(selections.Current(document));
    }

    [Fact]
    public void Repeating_an_unchanged_selection_does_not_take_over_and_a_closed_editor_falls_back()
    {
        var selections = new EditorSelections(new ManualClock());
        var (document, first, second) = (Guid.NewGuid(), Guid.NewGuid(), Guid.NewGuid());
        Guid[] older = [Guid.NewGuid()], newer = [Guid.NewGuid()];

        selections.Report(document, first, older);
        selections.Report(document, second, newer);
        selections.Report(document, first, older);
        Assert.Equal(newer, selections.Current(document)!.Ids);

        selections.Clear(document, second);
        Assert.Equal(older, selections.Current(document)!.Ids);
    }

    private sealed class ManualClock : TimeProvider
    {
        private DateTimeOffset now = DateTimeOffset.UnixEpoch;

        public void Advance(TimeSpan by) => now += by;

        public override DateTimeOffset GetUtcNow() => now;
    }
}
