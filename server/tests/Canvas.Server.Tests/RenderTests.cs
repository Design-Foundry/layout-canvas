using System.Net;
using System.Net.Http.Json;
using System.Text.Json;
using Canvas.Server.Documents;
using Canvas.Server.Mcp;
using Microsoft.Extensions.DependencyInjection;
using ModelContextProtocol;
using ModelContextProtocol.Protocol;

namespace Canvas.Server.Tests;

public sealed class RenderRequestsTests
{
    private static CancellationToken Ct => TestContext.Current.CancellationToken;
    private static readonly TimeSpan Short = TimeSpan.FromMilliseconds(150);
    private static readonly RenderedImage Image = new([1, 2, 3], 4, 5, 1);

    private static (RenderRequests Renders, EditorSelections Selections, Guid Document, Guid Editor) Live()
    {
        var selections = new EditorSelections(TimeProvider.System);
        var (document, editor) = (Guid.NewGuid(), Guid.NewGuid());
        selections.Report(document, editor, []);
        return (new RenderRequests(selections) { Timeout = TimeSpan.FromSeconds(5) }, selections, document, editor);
    }

    [Fact]
    public async Task A_request_is_claimed_by_the_live_editor_and_resolves_with_its_result()
    {
        var (renders, _, document, editor) = Live();
        var node = Guid.NewGuid();

        var waiting = renders.RequestAsync(document, node, 2, Ct);
        var claimed = await renders.ClaimAsync(document, editor, TimeSpan.FromSeconds(2), Ct);

        Assert.Equal((node, 2d, RenderRequests.MaxSize), (claimed!.NodeId, claimed.Scale, claimed.MaxSize));
        Assert.True(renders.Complete(document, claimed.Id, editor, Image, null));
        Assert.Equal(Image, await waiting);
    }

    [Fact]
    public async Task A_polling_editor_waits_for_a_request_that_arrives_later()
    {
        var (renders, _, document, editor) = Live();

        var claiming = renders.ClaimAsync(document, editor, TimeSpan.FromSeconds(5), Ct);
        var waiting = renders.RequestAsync(document, Guid.NewGuid(), 1, Ct);

        var claimed = await claiming;
        Assert.NotNull(claimed);
        renders.Complete(document, claimed.Id, editor, Image, null);
        await waiting;
    }

    [Fact]
    public async Task Only_the_live_editor_takes_a_request_and_only_once()
    {
        var (renders, selections, document, live) = Live();
        var other = Guid.NewGuid();
        selections.Report(document, other, [Guid.NewGuid()]);
        selections.Report(document, live, [Guid.NewGuid()]);

        var waiting = renders.RequestAsync(document, Guid.NewGuid(), 1, Ct);
        Assert.Null(await renders.ClaimAsync(document, other, Short, Ct));
        var claimed = await renders.ClaimAsync(document, live, Short, Ct);
        Assert.NotNull(claimed);
        Assert.Null(await renders.ClaimAsync(document, live, Short, Ct));
        Assert.False(renders.Complete(document, claimed.Id, other, Image, null));
        Assert.True(renders.Complete(document, claimed.Id, live, Image, null));
        await waiting;
    }

    [Fact]
    public async Task Without_a_live_editor_the_request_fails_at_once()
    {
        var renders = new RenderRequests(new EditorSelections(TimeProvider.System));

        var failure = await Assert.ThrowsAsync<RenderException>(() => renders.RequestAsync(Guid.NewGuid(), Guid.NewGuid(), 1, Ct));

        Assert.Equal(RenderRequests.NoEditor, failure.Message);
    }

    [Fact]
    public async Task A_request_nobody_answers_times_out_and_leaves_nothing_behind()
    {
        var (renders, _, document, editor) = Live();
        renders.Timeout = Short;

        await Assert.ThrowsAsync<RenderException>(() => renders.RequestAsync(document, Guid.NewGuid(), 1, Ct));

        Assert.Null(await renders.ClaimAsync(document, editor, TimeSpan.FromMilliseconds(20), Ct));
    }

    [Fact]
    public async Task An_editor_that_reports_an_error_fails_the_request_with_its_reason()
    {
        var (renders, _, document, editor) = Live();

        var waiting = renders.RequestAsync(document, Guid.NewGuid(), 1, Ct);
        var claimed = await renders.ClaimAsync(document, editor, TimeSpan.FromSeconds(2), Ct);
        renders.Complete(document, claimed!.Id, editor, null, "node not found");

        var failure = await Assert.ThrowsAsync<RenderException>(() => waiting);
        Assert.Contains("node not found", failure.Message);
    }

    [Fact]
    public async Task When_the_editor_goes_away_its_request_fails_without_waiting_for_the_timeout()
    {
        var (renders, selections, document, editor) = Live();

        var waiting = renders.RequestAsync(document, Guid.NewGuid(), 1, Ct);
        await renders.ClaimAsync(document, editor, TimeSpan.FromSeconds(2), Ct);
        selections.Clear(document, editor);
        renders.EditorGone(document, editor);

        await Assert.ThrowsAsync<RenderException>(() => waiting);
    }

    [Fact]
    public async Task When_the_last_editor_goes_away_unclaimed_requests_fail_too()
    {
        var (renders, selections, document, editor) = Live();

        var waiting = renders.RequestAsync(document, Guid.NewGuid(), 1, Ct);
        selections.Clear(document, editor);
        renders.EditorGone(document, editor);

        var failure = await Assert.ThrowsAsync<RenderException>(() => waiting);
        Assert.Equal(RenderRequests.NoEditor, failure.Message);
    }
}

public sealed class RenderToolTests(CanvasFactory factory) : IClassFixture<CanvasFactory>
{
    private static CancellationToken Ct => TestContext.Current.CancellationToken;
    private readonly HttpClient client = factory.CreateClient();
    private CanvasTools Tools => ActivatorUtilities.CreateInstance<CanvasTools>(factory.Services);

    private static AgentOp Op(string kind, object data) => new(kind, JsonSerializer.SerializeToElement(data));

    private async Task<(Guid Document, Guid Node)> DocumentWithFrame()
    {
        var document = (await Tools.CreateDocument("Render", Ct)).Id;
        var node = Guid.NewGuid();
        await Tools.ApplyOps(document, [Op("CreateNode", new { node = new { id = node, type = "frame", name = "Card", w = 40, h = 20 } })], Ct);
        return (document, node);
    }

    private async Task<Guid> OpenEditor(Guid document)
    {
        var editor = Guid.NewGuid();
        await client.PutAsJsonAsync($"/api/documents/{document}/selection", new SelectionReport(editor, []), Ct);
        return editor;
    }

    private Task FakeEditor(Guid document, Guid editor, Func<RenderRequest, RenderResult> answer) => Task.Run(async () =>
    {
        var response = await client.GetAsync($"/api/documents/{document}/render-requests?editorId={editor}&wait=5", Ct);
        var request = (await response.Content.ReadFromJsonAsync<RenderRequest>(Ct))!;
        await client.PutAsJsonAsync($"/api/documents/{document}/render-requests/{request.Id}/result", answer(request), Ct);
    }, Ct);

    [Fact]
    public async Task Render_returns_the_editors_png_and_a_line_about_it()
    {
        var (document, node) = await DocumentWithFrame();
        var editor = await OpenEditor(document);
        var png = Convert.ToBase64String([137, 80, 78, 71]);
        RenderRequest? seen = null;
        var editorWork = FakeEditor(document, editor, r =>
        {
            seen = r;
            return new RenderResult(editor, png, 80, 40, 2, null);
        });

        var result = await Tools.Render(document, node, 2, Ct);
        await editorWork;

        Assert.Equal((node, 2d), (seen!.NodeId, seen.Scale));
        var image = Assert.IsType<ImageContentBlock>(result.Content[0]);
        Assert.Equal("image/png", image.MimeType);
        Assert.Equal([137, 80, 78, 71], Convert.FromBase64String(System.Text.Encoding.UTF8.GetString(image.Data.Span)));
        Assert.Equal("Card: 80x40 px at 2x.", Assert.IsType<TextContentBlock>(result.Content[1]).Text);
    }

    [Fact]
    public async Task A_lower_scale_than_asked_is_reported()
    {
        var (document, node) = await DocumentWithFrame();
        var editor = await OpenEditor(document);
        var editorWork = FakeEditor(document, editor, _ => new RenderResult(editor, "AQID", 2048, 1024, 1.5, null));

        var result = await Tools.Render(document, node, 3, Ct);
        await editorWork;

        var text = Assert.IsType<TextContentBlock>(result.Content[1]).Text;
        Assert.Contains("2048x1024 px at 1.5x", text);
        Assert.Contains("reduced from 3x", text);
    }

    [Fact]
    public async Task Render_without_an_open_editor_says_to_open_one()
    {
        var (document, node) = await DocumentWithFrame();

        var failure = await Assert.ThrowsAsync<McpException>(() => Tools.Render(document, node, 1, Ct));

        Assert.Contains("Open the document in the editor", failure.Message);
    }

    [Fact]
    public async Task Render_refuses_unknown_documents_nodes_and_scales()
    {
        var (document, node) = await DocumentWithFrame();
        await OpenEditor(document);

        await Assert.ThrowsAsync<McpException>(() => Tools.Render(Guid.NewGuid(), node, 1, Ct));
        var unknown = await Assert.ThrowsAsync<McpException>(() => Tools.Render(document, Guid.NewGuid(), 1, Ct));
        Assert.Contains("does not exist", unknown.Message);
        await Assert.ThrowsAsync<McpException>(() => Tools.Render(document, node, 0, Ct));
        await Assert.ThrowsAsync<McpException>(() => Tools.Render(document, node, 3.5, Ct));
    }

    [Fact]
    public async Task A_render_failure_in_the_editor_reaches_the_agent()
    {
        var (document, node) = await DocumentWithFrame();
        var editor = await OpenEditor(document);
        var editorWork = FakeEditor(document, editor, _ => new RenderResult(editor, null, 0, 0, 0, "node not found"));

        var failure = await Assert.ThrowsAsync<McpException>(() => Tools.Render(document, node, 1, Ct));
        await editorWork;

        Assert.Contains("node not found", failure.Message);
    }

    [Fact]
    public async Task An_editor_that_never_answers_times_out()
    {
        var (document, node) = await DocumentWithFrame();
        await OpenEditor(document);
        var renders = factory.Services.GetRequiredService<RenderRequests>();
        var before = renders.Timeout;
        renders.Timeout = TimeSpan.FromMilliseconds(200);
        try
        {
            await Assert.ThrowsAsync<McpException>(() => Tools.Render(document, node, 1, Ct));
        }
        finally
        {
            renders.Timeout = before;
        }
    }

    [Fact]
    public async Task Results_for_unknown_requests_are_not_found_and_idle_polls_return_no_content()
    {
        var document = Guid.NewGuid();
        var body = new RenderResult(Guid.NewGuid(), "AQID", 1, 1, 1, null);

        Assert.Equal(HttpStatusCode.NotFound, (await client.PutAsJsonAsync($"/api/documents/{document}/render-requests/{Guid.NewGuid()}/result", body, Ct)).StatusCode);
        Assert.Equal(HttpStatusCode.NoContent, (await client.GetAsync($"/api/documents/{document}/render-requests?editorId={Guid.NewGuid()}&wait=0", Ct)).StatusCode);
    }
}
