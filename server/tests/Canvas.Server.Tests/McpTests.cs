using System.Net;
using System.Text.Json;
using Canvas.Server.Documents;
using Canvas.Server.Mcp;
using Microsoft.Extensions.DependencyInjection;
using ModelContextProtocol;

namespace Canvas.Server.Tests;

public sealed class OpBatchTests
{
    private static AgentOp Op(string kind, object data) => new(kind, JsonSerializer.SerializeToElement(data));

    [Fact]
    public void Placeholders_become_one_id_each_across_the_batch()
    {
        var (ops, ids) = OpBatch.Prepare(
        [
            Op("CreateNode", new { node = new { id = "$card", type = "frame" } }),
            Op("CreateNode", new { node = new { id = "$title", type = "text", parent = "$card" } }),
            Op("CreateToken", new { token = new { id = "$ink", source = new Dictionary<string, string> { ["$dark"] = "$base" } } }),
        ], new Dictionary<Guid, string>());

        Assert.Equal(["$base", "$card", "$dark", "$ink", "$title"], ids.Keys.Order());
        Assert.Equal(ids["$card"].ToString(), ops[1].Data.GetProperty("node").GetProperty("parent").GetString());
        Assert.True(ops[2].Data.GetProperty("token").GetProperty("source").TryGetProperty(ids["$dark"].ToString(), out var source));
        Assert.Equal(ids["$base"].ToString(), source.GetString());
        Assert.Equal(3, ops.Select(o => o.Id).Distinct().Count());
    }

    [Fact]
    public void Missing_orders_follow_existing_and_earlier_siblings()
    {
        var parent = Guid.NewGuid();
        var (ops, _) = OpBatch.Prepare(
        [
            Op("CreateNode", new { node = new { id = "$a", parent } }),
            Op("CreateNode", new { node = new { id = "$b", parent } }),
            Op("CreateNode", new { node = new { id = "$c", parent, order = "b0" } }),
            Op("CreateNode", new { node = new { id = "$d", parent } }),
            Op("CreateNode", new { node = new { id = "$top" } }),
        ], new Dictionary<Guid, string> { [parent] = "a4" });

        var orders = ops.Select(o => o.Data.GetProperty("node").GetProperty("order").GetString()).ToList();
        Assert.Equal(["a5", "a6", "b0", "b0V", "a0"], orders);
    }

    [Theory]
    [InlineData(null, "a0")]
    [InlineData("a0", "a1")]
    [InlineData("a9", "aA")]
    [InlineData("ay", "az")]
    [InlineData("az", "azV")]
    [InlineData("a0V", "a0VV")]
    public void After_sorts_after_the_last_sibling(string? last, string expected)
    {
        Assert.Equal(expected, OpBatch.After(last));
        Assert.True(string.CompareOrdinal(expected, last ?? "") > 0);
    }
}

public sealed class McpToolsTests(CanvasFactory factory) : IClassFixture<CanvasFactory>
{
    private static CancellationToken Ct => TestContext.Current.CancellationToken;
    private CanvasTools Tools => ActivatorUtilities.CreateInstance<CanvasTools>(factory.Services);

    private static AgentOp Op(string kind, object data) => new(kind, JsonSerializer.SerializeToElement(data));

    private static object Text(string content) =>
        new { content, font = "inter-regular-v1", fontSize = 14, lineHeight = 1.4, letterSpacing = 0, align = "left" };

    [Fact]
    public async Task Ops_build_a_laid_out_frame_and_report_refusals()
    {
        var document = await Tools.CreateDocument("Card", Ct);
        var padding = new[] { 16, 16, 16, 16 };

        var result = await Tools.ApplyOps(document.Id,
        [
            Op("CreateNode", new { node = new { id = "$card", type = "frame", name = "Card", w = 200, h = 10, heightSizing = "hug",
                layout = new { type = "column", gap = 8, padding, justify = "start", align = "stretch" } } }),
            Op("CreateNode", new { node = new { id = "$title", type = "text", parent = "$card", w = 10, h = 10, widthSizing = "fill", heightSizing = "hug", text = Text("Pedidos") } }),
            Op("SetProps", new { id = "$title", patch = new { cornerRadius = 4 } }),
        ], Ct);

        Assert.Equal(2, result.GetProperty("applied").GetInt32());
        var card = result.GetProperty("ids").GetProperty("$card").GetGuid();
        var refused = result.GetProperty("results")[2];
        Assert.False(refused.GetProperty("applied").GetBoolean());
        Assert.Equal("ops[2]", refused.GetProperty("error").GetProperty("path").GetString());
        Assert.StartsWith("text, lines, paths and images take no stroke", refused.GetProperty("error").GetProperty("message").GetString());

        var malformed = await Assert.ThrowsAsync<McpException>(() => Tools.ApplyOps(document.Id,
        [
            Op("SetProps", new { id = card, patch = new { name = "Tarjeta" } }),
            Op("SetProps", new { id = card, patch = new { text = new { content = "x", font = "inter-regular-v1", fontSize = 14, lineHeight = 1.4, letterSpacing = -1, align = "left" } } }),
        ], Ct));
        Assert.Equal("Nothing was applied: ops[1].patch.text: letterSpacing must be between 0 and 128", malformed.Message);

        var layout = await Tools.GetLayout(document.Id, card, null, Ct);
        Assert.Equal(2, layout.GetArrayLength());
        Assert.Equal(200, layout[0].GetProperty("w").GetDouble());
        Assert.Equal(168, layout[1].GetProperty("w").GetDouble());
        Assert.Equal(16, layout[1].GetProperty("x").GetDouble() - layout[0].GetProperty("x").GetDouble());
        Assert.False(layout[1].TryGetProperty("overflows", out _));

        var read = await Tools.GetDocument(document.Id, null, Ct);
        Assert.Equal(2, read.GetProperty("layers").GetArrayLength());
    }

    [Fact]
    public async Task Import_errors_name_where_and_why()
    {
        var snapshot = JsonSerializer.SerializeToElement(new
        {
            version = 5, modes = Array.Empty<object>(), tokens = Array.Empty<object>(),
            nodes = new[] { new { id = Guid.NewGuid(), type = "text", order = "a0", text = new { content = "x", font = "inter-regular-v1", fontSize = 12, lineHeight = 1.4, letterSpacing = -2, align = "left" } } },
        });

        var error = await Assert.ThrowsAsync<McpException>(() => Tools.ImportDocument("Bad", snapshot, Ct));
        Assert.Contains("nodes[0].text", error.Message);
        Assert.Contains("letterSpacing must be between 0 and 128", error.Message);
    }

    [Fact]
    public async Task Unknown_documents_are_reported()
    {
        await Assert.ThrowsAsync<McpException>(() => Tools.GetLayout(Guid.NewGuid(), null, null, Ct));
        Assert.Contains("SetComponentDefinition", Tools.GetFormatGuide());
    }

    [Fact]
    public async Task The_endpoint_refuses_requests_from_other_machines()
    {
        var response = await factory.CreateClient().PostAsync("/mcp", new StringContent("{}", System.Text.Encoding.UTF8, "application/json"), Ct);
        Assert.Equal(HttpStatusCode.Forbidden, response.StatusCode);
    }
}
