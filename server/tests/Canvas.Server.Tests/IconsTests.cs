using System.Net;
using System.Net.Http.Json;
using System.Text.Json;
using Canvas.Server.Documents;
using Canvas.Server.Icons;
using Canvas.Server.Mcp;
using Canvas.Server.Storage;
using Canvas.Server.VisualCore;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Options;
using ModelContextProtocol;

namespace Canvas.Server.Tests;

public sealed class IconsTests
{
    private static CancellationToken Ct => TestContext.Current.CancellationToken;

    private static readonly string Commit = new MaterialSymbolsOptions().Commit;

    private const string Glyphs = "/api/icons/material-symbols/glyphs";

    private static async Task<IconSearchResponse> Search(HttpClient client, string query) =>
        (await client.GetFromJsonAsync<IconSearchResponse>($"{Glyphs}{query}", Ct))!;

    private static async Task<DocumentSummary> NewDocument(HttpClient client) =>
        (await (await client.PostAsJsonAsync("/api/documents", new CreateDocumentRequest("Icons"), Ct)).Content.ReadFromJsonAsync<DocumentSummary>(Ct))!;

    private static OpInput CreateIcon(string order, object icon) => new(Guid.NewGuid(), "CreateNode", JsonSerializer.SerializeToElement(new
    {
        node = new { id = Guid.NewGuid(), type = "icon", order, name = "Icon", w = 24.0, h = 24.0, icon },
    }));

    [Fact]
    public async Task The_default_commit_is_the_one_visualcore_pins_the_built_in_set_to()
    {
        await using var factory = new CanvasFactory();
        var client = factory.CreateClient();
        var snapshot = JsonSerializer.SerializeToElement(new
        {
            version = 27,
            modes = Array.Empty<object>(),
            tokens = Array.Empty<object>(),
            nodes = new object[] { new { id = Guid.NewGuid(), type = "icon", order = "a0", icon = "material-symbols/home" } },
        });
        var imported = await client.PostAsJsonAsync("/api/documents/import", new ImportDocumentRequest("Icons", snapshot), Ct);
        var document = (await imported.Content.ReadFromJsonAsync<DocumentSummary>(Ct))!;

        var detail = await client.GetFromJsonAsync<JsonElement>($"/api/documents/{document.Id}", Ct);
        var sets = detail.GetProperty("snapshot").GetProperty("data");
        Assert.Equal(27, sets.GetProperty("version").GetInt32());
        Assert.Equal(Commit, sets.GetProperty("iconSets")[0].GetProperty("version").GetString());
    }

    [Fact]
    public async Task Search_lists_prefix_matches_before_contains_matches_and_pages_them()
    {
        await using var factory = new CanvasFactory();
        var client = factory.CreateClient();

        var arrows = await Search(client, "?query=ar");
        Assert.Equal(Commit, arrows.Version);
        Assert.Equal(5, arrows.Total);
        Assert.Equal(["arrow_back", "arrow_forward", "arrow_upward", "keyboard_arrow_left", "search"], arrows.Items.Select(i => i.Glyph));

        var page = await Search(client, "?query=ar&offset=1&limit=2");
        Assert.Equal(5, page.Total);
        Assert.Equal(1, page.Offset);
        Assert.Equal(["arrow_forward", "arrow_upward"], page.Items.Select(i => i.Glyph));

        Assert.Equal(["arrow_back"], (await Search(client, "?query=Arrow%20Back")).Items.Select(i => i.Glyph));
        Assert.Equal(["10k"], (await Search(client, "?query=10")).Items.Select(i => i.Glyph));
        Assert.Empty((await Search(client, "?query=zzz")).Items);
        Assert.Equal(9, (await Search(client, "")).Total);
        Assert.Equal(["10k", "arrow_back"], (await Search(client, "?limit=2")).Items.Select(i => i.Glyph));

        var inventories = factory.GitHub.Requests.Count(r => r.AbsolutePath.Contains("/git/trees/"));
        Assert.Equal(1, inventories);
        Assert.True(File.Exists(Path.Combine(factory.IconsPath, "material-symbols", Commit, "glyphs.txt")));
    }

    [Theory]
    [InlineData("?offset=-1")]
    [InlineData("?version=main")]
    public async Task Search_refuses_bad_paging_and_versions(string query)
    {
        await using var factory = new CanvasFactory();

        var response = await factory.CreateClient().GetAsync($"{Glyphs}{query}", Ct);

        Assert.Equal(HttpStatusCode.BadRequest, response.StatusCode);
        Assert.Empty(factory.GitHub.Requests);
    }

    [Theory]
    [InlineData("", "outlined/arrow_back_24px.svg")]
    [InlineData("?style=rounded&FILL=1&wght=500", "rounded/arrow_back_wght500fill1_24px.svg")]
    [InlineData("?style=sharp&fill=1&opsz=48", "sharp/arrow_back_fill1_48px.svg")]
    [InlineData("?wght=100&opsz=20", "outlined/arrow_back_wght100_20px.svg")]
    public async Task A_glyph_svg_is_read_from_the_file_the_source_names_for_its_axes(string query, string file)
    {
        await using var factory = new CanvasFactory();

        var response = await factory.CreateClient().GetAsync($"{Glyphs}/arrow_back{query}", Ct);

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
        Assert.Equal("image/svg+xml", response.Content.Headers.ContentType?.MediaType);
        Assert.Equal(FakeGitHub.IconSvg("arrow_back", file), await response.Content.ReadAsStringAsync(Ct));
        Assert.Equal("public, max-age=3600", response.Headers.CacheControl?.ToString());
    }

    [Fact]
    public async Task A_pinned_version_makes_the_svg_immutable_and_files_are_cached_on_disk()
    {
        await using var factory = new CanvasFactory();
        var client = factory.CreateClient();

        var first = await client.GetAsync($"{Glyphs}/home?version={Commit}", Ct);
        var requests = factory.GitHub.Requests.Count;
        var second = await client.GetAsync($"{Glyphs}/home?version={Commit}", Ct);

        Assert.Equal("public, max-age=31536000, immutable", first.Headers.CacheControl?.ToString());
        Assert.Equal(await first.Content.ReadAsStringAsync(Ct), await second.Content.ReadAsStringAsync(Ct));
        Assert.Equal(requests, factory.GitHub.Requests.Count);
        Assert.True(File.Exists(Path.Combine(factory.IconsPath, "material-symbols", Commit, "outlined", "home_24px.svg")));
    }

    [Theory]
    [InlineData("arrow_back?style=bold")]
    [InlineData("arrow_back?FILL=2")]
    [InlineData("arrow_back?wght=450")]
    [InlineData("arrow_back?opsz=30")]
    [InlineData("Arrow_Back")]
    [InlineData("arrow_back?version=main")]
    public async Task Axis_values_the_source_does_not_have_are_refused_without_contacting_github(string path)
    {
        await using var factory = new CanvasFactory();

        var response = await factory.CreateClient().GetAsync($"{Glyphs}/{path}", Ct);

        Assert.Equal(HttpStatusCode.BadRequest, response.StatusCode);
        Assert.Empty(factory.GitHub.Requests);
    }

    [Fact]
    public async Task A_glyph_or_axis_combination_the_source_lacks_is_not_found()
    {
        await using var factory = new CanvasFactory();
        var client = factory.CreateClient();

        Assert.Equal(HttpStatusCode.NotFound, (await client.GetAsync($"{Glyphs}/glyph_nobody_has", Ct)).StatusCode);
        Assert.Equal(HttpStatusCode.NotFound, (await client.GetAsync($"{Glyphs}/no_svg_glyph", Ct)).StatusCode);
        Assert.Equal(HttpStatusCode.NotFound, (await client.GetAsync($"{Glyphs}/arrow_back?wght=700", Ct)).StatusCode);
        Assert.DoesNotContain(factory.GitHub.Requests, r => r.AbsolutePath.Contains("glyph_nobody_has"));

        var requests = factory.GitHub.Requests.Count;
        Assert.Equal(HttpStatusCode.NotFound, (await client.GetAsync($"{Glyphs}/arrow_back?wght=700", Ct)).StatusCode);
        Assert.Equal(requests, factory.GitHub.Requests.Count);
    }

    [Fact]
    public async Task An_upstream_failure_is_a_bad_gateway_and_leaves_nothing_cached()
    {
        await using var factory = new CanvasFactory();
        factory.GitHub.Respond("symbols/web/home/materialsymbolsoutlined/home_24px.svg", HttpStatusCode.InternalServerError);
        var client = factory.CreateClient();

        Assert.Equal(HttpStatusCode.BadGateway, (await client.GetAsync($"{Glyphs}/home", Ct)).StatusCode);
        Assert.False(File.Exists(Path.Combine(factory.IconsPath, "material-symbols", Commit, "outlined", "home_24px.svg")));

        factory.GitHub.ServeIcon("home", "outlined", "home_24px.svg");
        Assert.Equal(HttpStatusCode.OK, (await client.GetAsync($"{Glyphs}/home", Ct)).StatusCode);
    }

    [Fact]
    public async Task A_file_that_is_not_an_svg_or_is_too_large_is_refused()
    {
        await using var factory = new CanvasFactory();
        factory.GitHub.ServeRaw("symbols/web/home/materialsymbolsoutlined/home_24px.svg", "<html></html>"u8.ToArray());
        factory.GitHub.ServeRaw("symbols/web/search/materialsymbolsoutlined/search_24px.svg", new byte[IconFileStore.MaxFileBytes + 1]);
        var client = factory.CreateClient();

        Assert.Equal(HttpStatusCode.BadGateway, (await client.GetAsync($"{Glyphs}/home", Ct)).StatusCode);
        Assert.Equal(HttpStatusCode.BadGateway, (await client.GetAsync($"{Glyphs}/search", Ct)).StatusCode);
    }

    [Fact]
    public async Task Only_the_allowlisted_github_hosts_are_contacted()
    {
        await using var factory = new CanvasFactory();
        var client = factory.CreateClient();
        await client.GetAsync($"{Glyphs}?query=home", Ct);
        await client.GetAsync($"{Glyphs}/home", Ct);

        Assert.NotEmpty(factory.GitHub.Requests);
        Assert.All(factory.GitHub.Requests, r => Assert.Contains(r.Host, MaterialSymbolsProvider.AllowedHosts));
        Assert.All(factory.GitHub.Requests, r => Assert.Equal(Uri.UriSchemeHttps, r.Scheme));
        Assert.Equal(["raw.githubusercontent.com", "api.github.com"], MaterialSymbolsProvider.AllowedHosts);
        Assert.All(factory.GitHub.Requests.Where(r => r.Host == "raw.githubusercontent.com"),
            r => Assert.StartsWith($"/google/material-design-icons/{Commit}/symbols/web/", r.AbsolutePath));
        Assert.All(factory.GitHub.Requests.Where(r => r.Host == "api.github.com"),
            r => Assert.Equal($"/repos/google/material-design-icons/git/trees/{Commit}:symbols/web", Uri.UnescapeDataString(r.AbsolutePath)));
    }

    [Fact]
    public async Task The_icon_folder_defaults_to_icons_next_to_the_fonts_folder()
    {
        await using var factory = new CanvasFactory();
        var environment = factory.Services.GetRequiredService<IHostEnvironment>();
        var root = Path.Combine(Path.GetTempPath(), $"canvas-store-{Guid.NewGuid():N}");

        var next = new IconFileStore(Options.Create(new StorageOptions { Fonts = Path.Combine(root, "fonts") }), environment);
        var explicitFolder = new IconFileStore(Options.Create(new StorageOptions { Fonts = Path.Combine(root, "fonts"), Icons = Path.Combine(root, "elsewhere") }), environment);
        var relative = new IconFileStore(Options.Create(new StorageOptions { Fonts = "data/fonts" }), environment);

        Assert.Equal(Path.Combine(root, "icons"), next.Root);
        Assert.Equal(Path.Combine(root, "elsewhere"), explicitFolder.Root);
        Assert.Equal(Path.Combine(environment.ContentRootPath, "data", "icons"), relative.Root);
    }

    [Fact]
    public async Task The_store_refuses_paths_that_leave_it()
    {
        await using var factory = new CanvasFactory();
        var store = factory.Services.GetRequiredService<IconFileStore>();

        await Assert.ThrowsAsync<InvalidOperationException>(() => store.ReadAsync("material-symbols", Commit, "../../../outside.svg", Ct));
    }

    [Fact]
    public async Task Appending_an_icon_registers_its_svg_natively_before_the_op_applies_and_only_once()
    {
        await using var factory = new CanvasFactory();
        var client = factory.CreateClient();
        var document = await NewDocument(client);
        var key = NativeIcons.SourceKey("\"material-symbols/keyboard_arrow_left?style=rounded&fill=1&wght=500\"")!;
        Assert.Equal("material-symbols/keyboard_arrow_left?style=rounded&FILL=1&wght=500&opsz=24", key);

        var appended = await client.PostAsJsonAsync($"/api/documents/{document.Id}/ops",
            new AppendOpsRequest([CreateIcon("a0", "material-symbols/keyboard_arrow_left?style=rounded&fill=1&wght=500")]), Ct);

        Assert.Null((await appended.Content.ReadFromJsonAsync<AppendOpsResponse>(Ct))!.Acks.Single().Error);
        var svg = FakeGitHub.IconSvg("keyboard_arrow_left", "rounded/keyboard_arrow_left_wght500fill1_24px.svg");
        Assert.NotNull(NativeIcons.Register(key, FakeGitHub.IconSvg("keyboard_arrow_left", "other")));
        Assert.Null(NativeIcons.Register(key, svg));

        var requests = factory.GitHub.Requests.Count;
        await client.PostAsJsonAsync($"/api/documents/{document.Id}/ops",
            new AppendOpsRequest([CreateIcon("a1", new { glyph = "keyboard_arrow_left", axes = new { style = "rounded", FILL = 1, wght = 500 } })]), Ct);
        Assert.Equal(requests, factory.GitHub.Requests.Count);
    }

    [Fact]
    public async Task Opening_a_document_registers_the_icons_of_its_snapshot()
    {
        await using var factory = new CanvasFactory();
        var client = factory.CreateClient();
        var snapshot = JsonSerializer.SerializeToElement(new
        {
            version = 27,
            modes = Array.Empty<object>(),
            tokens = Array.Empty<object>(),
            nodes = new object[]
            {
                new { id = Guid.NewGuid(), type = "icon", order = "a0", w = 24.0, h = 24.0, icon = "material-symbols/arrow_upward?style=sharp&opsz=48&fill=1" },
            },
        });
        var imported = await client.PostAsJsonAsync("/api/documents/import", new ImportDocumentRequest("Icons", snapshot), Ct);
        Assert.Equal(HttpStatusCode.Created, imported.StatusCode);
        var document = (await imported.Content.ReadFromJsonAsync<DocumentSummary>(Ct))!;

        var node = new OpInput(Guid.NewGuid(), "CreateNode", JsonSerializer.SerializeToElement(new
        {
            node = new { id = Guid.NewGuid(), type = "frame", order = "V", name = "Card", w = 200.0, h = 100.0 },
        }));
        await client.PostAsJsonAsync($"/api/documents/{document.Id}/ops", new AppendOpsRequest([node]), Ct);

        var key = "material-symbols/arrow_upward?style=sharp&FILL=1&wght=400&opsz=48";
        Assert.NotNull(NativeIcons.Register(key, FakeGitHub.IconSvg("arrow_upward", "other")));
        Assert.Null(NativeIcons.Register(key, FakeGitHub.IconSvg("arrow_upward", "sharp/arrow_upward_fill1_48px.svg")));
    }

    [Fact]
    public async Task A_glyph_the_source_lacks_loads_and_is_not_an_error()
    {
        await using var factory = new CanvasFactory();
        var client = factory.CreateClient();
        var document = await NewDocument(client);

        var appended = await client.PostAsJsonAsync($"/api/documents/{document.Id}/ops",
            new AppendOpsRequest([CreateIcon("a0", "material-symbols/glyph_nobody_has"), CreateIcon("a1", "material-symbols/no_svg_glyph")]), Ct);

        Assert.All((await appended.Content.ReadFromJsonAsync<AppendOpsResponse>(Ct))!.Acks, ack => Assert.Null(ack.Error));
        var store = factory.Services.GetRequiredService<DocumentStore>();
        var logged = (await store.GetOpsAsync(document.Id, 0, 100, Ct)).Page!.Ops;
        Assert.Equal(2, logged.Count);
        Assert.Equal("glyph_nobody_has", logged[0].Data.GetProperty("node").GetProperty("icon").GetProperty("glyph").GetString());
    }

    [Fact]
    public async Task An_icon_that_cannot_be_fetched_does_not_fail_the_document_operation()
    {
        await using var factory = new CanvasFactory();
        factory.GitHub.Respond("symbols/web/settings/materialsymbolsoutlined/settings_24px.svg", HttpStatusCode.InternalServerError);
        var client = factory.CreateClient();
        var document = await NewDocument(client);

        var appended = await client.PostAsJsonAsync($"/api/documents/{document.Id}/ops",
            new AppendOpsRequest([CreateIcon("a0", "material-symbols/settings")]), Ct);

        Assert.Equal(HttpStatusCode.OK, appended.StatusCode);
        Assert.Null((await appended.Content.ReadFromJsonAsync<AppendOpsResponse>(Ct))!.Acks.Single().Error);
    }
}

public sealed class IconToolsTests(CanvasFactory factory) : IClassFixture<CanvasFactory>
{
    private static CancellationToken Ct => TestContext.Current.CancellationToken;
    private CanvasTools Tools => ActivatorUtilities.CreateInstance<CanvasTools>(factory.Services);

    private static AgentOp Op(string kind, object data) => new(kind, JsonSerializer.SerializeToElement(data));

    [Fact]
    public async Task One_apply_ops_creates_icons_from_the_string_form_and_stores_the_structured_form()
    {
        var document = await Tools.CreateDocument("Icons", Ct);

        var result = await Tools.ApplyOps(document.Id,
        [
            Op("CreateToken", new { token = new { id = "$ink", type = "color", identifier = "ink", order = "a0", value = new { l = 0.3, c = 0.1, h = 250 } } }),
            Op("CreateNode", new { node = new { id = "$bar", type = "frame", name = "Bar", w = 200, h = 40 } }),
            Op("CreateNode", new { node = new { id = "$back", type = "icon", name = "Back", parent = "$bar", icon = "material-symbols/arrow_back", fill = new { token = "$ink" } } }),
            Op("CreateNode", new { node = new { id = "$home", type = "icon", name = "Home", parent = "$bar", icon = "material-symbols/home?style=rounded&fill=1&wght=500", w = 32, h = 32, x = 40 } }),
            Op("CreateNode", new { node = new { id = "$gone", type = "icon", name = "Gone", parent = "$bar", icon = "material-symbols/glyph_nobody_has", x = 80 } }),
        ], Ct);

        Assert.Equal(5, result.GetProperty("applied").GetInt32());
        var ids = result.GetProperty("ids");
        var nodes = await Tools.GetNodes(document.Id, [ids.GetProperty("$back").GetGuid(), ids.GetProperty("$home").GetGuid(), ids.GetProperty("$gone").GetGuid()], null, Ct);

        var back = nodes[0];
        Assert.Equal("icon", back.GetProperty("type").GetString());
        Assert.Equal("arrow_back", back.GetProperty("icon").GetProperty("glyph").GetString());
        Assert.Equal("00000000-0000-0000-0000-000000000200", back.GetProperty("icon").GetProperty("set").GetString());
        Assert.False(back.GetProperty("icon").TryGetProperty("axes", out _));
        Assert.Equal("material-symbols/arrow_back", back.GetProperty("iconPath").GetString());
        Assert.Equal(24, back.GetProperty("box").GetProperty("w").GetDouble());
        Assert.Equal(24, back.GetProperty("box").GetProperty("h").GetDouble());
        Assert.Equal(ids.GetProperty("$ink").GetString(), back.GetProperty("fill").GetProperty("token").GetString());

        var home = nodes[1].GetProperty("icon");
        Assert.Equal("rounded", home.GetProperty("axes").GetProperty("style").GetString());
        Assert.Equal(1, home.GetProperty("axes").GetProperty("FILL").GetInt32());
        Assert.Equal(500, home.GetProperty("axes").GetProperty("wght").GetInt32());
        Assert.Equal("material-symbols/home?style=rounded&FILL=1&wght=500", nodes[1].GetProperty("iconPath").GetString());
        Assert.Equal("glyph_nobody_has", nodes[2].GetProperty("icon").GetProperty("glyph").GetString());

        var store = factory.Services.GetRequiredService<DocumentStore>();
        var logged = (await store.GetOpsAsync(document.Id, 0, 100, Ct)).Page!.Ops.Where(o => o.Kind == "CreateNode").ToList();
        Assert.All(logged.Where(o => o.Data.GetProperty("node").GetProperty("type").GetString() == "icon"),
            o => Assert.Equal(JsonValueKind.Object, o.Data.GetProperty("node").GetProperty("icon").ValueKind));
    }

    [Fact]
    public async Task An_icon_can_be_swapped_by_a_patch_in_string_form_and_a_bad_one_is_refused_with_its_reason()
    {
        var document = await Tools.CreateDocument("Swap", Ct);
        var created = await Tools.ApplyOps(document.Id,
            [Op("CreateNode", new { node = new { id = "$i", type = "icon", icon = "material-symbols/search" } })], Ct);
        var icon = created.GetProperty("ids").GetProperty("$i").GetGuid();

        var badAxis = await Assert.ThrowsAsync<McpException>(() => Tools.ApplyOps(document.Id,
            [Op("SetProps", new { id = icon, patch = new { icon = "material-symbols/settings?wght=450" } })], Ct));
        Assert.Equal("Nothing was applied: ops[0].patch.icon: a modifier has a value the set does not offer for that axis", badAxis.Message);
        var badSet = await Assert.ThrowsAsync<McpException>(() => Tools.ApplyOps(document.Id,
            [Op("SetProps", new { id = icon, patch = new { icon = "lucide/settings" } })], Ct));
        Assert.Contains("the icon set of the path is unknown", badSet.Message);

        var result = await Tools.ApplyOps(document.Id,
        [
            Op("SetProps", new { id = icon, patch = new { icon = "material-symbols/settings?wght=300" } }),
            Op("SetProps", new { id = icon, patch = new { stroke = new { width = 2 } } }),
        ], Ct);

        Assert.Equal(1, result.GetProperty("applied").GetInt32());
        Assert.StartsWith("icons take no stroke", result.GetProperty("results")[1].GetProperty("error").GetProperty("message").GetString());
        var node = (await Tools.GetNodes(document.Id, [icon], null, Ct))[0];
        Assert.Equal("settings", node.GetProperty("icon").GetProperty("glyph").GetString());
        Assert.Equal(300, node.GetProperty("icon").GetProperty("axes").GetProperty("wght").GetInt32());
    }

    [Fact]
    public async Task Search_icons_lists_glyphs_and_the_axes_the_set_offers()
    {
        var found = await Tools.SearchIcons("arrow", 0, 2, Ct);

        Assert.Equal("material-symbols", found.GetProperty("set").GetString());
        Assert.Equal(4, found.GetProperty("total").GetInt32());
        Assert.Equal(["arrow_back", "arrow_forward"], found.GetProperty("glyphs").EnumerateArray().Select(g => g.GetString()));
        var axes = found.GetProperty("axes");
        Assert.Equal(["outlined", "rounded", "sharp"], axes.GetProperty("style").EnumerateArray().Select(v => v.GetString()));
        Assert.Equal([0, 1], axes.GetProperty("FILL").EnumerateArray().Select(v => v.GetInt32()));
        Assert.Equal([100, 200, 300, 400, 500, 600, 700], axes.GetProperty("wght").EnumerateArray().Select(v => v.GetInt32()));
        Assert.Equal([20, 24, 40, 48], axes.GetProperty("opsz").EnumerateArray().Select(v => v.GetInt32()));
        Assert.Equal(["keyboard_arrow_left"], (await Tools.SearchIcons("arrow", 3, 10, Ct)).GetProperty("glyphs").EnumerateArray().Select(g => g.GetString()));
        await Assert.ThrowsAsync<McpException>(() => Tools.SearchIcons("arrow", -1, 10, Ct));
    }

    [Fact]
    public void The_format_guide_describes_icons()
    {
        var guide = Tools.GetFormatGuide();

        Assert.Contains("material-symbols/", guide);
        Assert.Contains("search_icons", guide);
    }
}
