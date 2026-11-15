using System.Net;
using System.Net.Http.Json;
using System.Security.Cryptography;
using System.Text.Json;
using Canvas.Server.Documents;
using Canvas.Server.Fonts;
using Canvas.Server.VisualCore;

namespace Canvas.Server.Tests;

public sealed class GoogleFontsTests
{
    private const string Commit = FakeGitHub.Commit;
    private static CancellationToken Ct => TestContext.Current.CancellationToken;

    private static string Sha(byte[] bytes) => Convert.ToHexStringLower(SHA256.HashData(bytes));

    private static string FilesUrl(string commit, string key) => $"/api/fonts/googlefonts/{commit}/{Uri.EscapeDataString(key)}/files";

    private static string UniqueCommit() => Guid.NewGuid().ToString("N") + "00000000";

    [Fact]
    public async Task Catalog_search_matches_display_names_case_insensitively()
    {
        await using var factory = new CanvasFactory();
        var client = factory.CreateClient();

        var all = (await client.GetFromJsonAsync<GoogleFamiliesResponse>("/api/fonts/google/families", Ct))!;
        var mono = (await client.GetFromJsonAsync<GoogleFamiliesResponse>("/api/fonts/google/families?query=MONO", Ct))!;
        var limited = (await client.GetFromJsonAsync<GoogleFamiliesResponse>("/api/fonts/google/families?limit=2", Ct))!;

        Assert.Equal(Commit, all.Version);
        Assert.Equal(
            ["IBM Plex Sans", "Old Static", "Open Sans", "Roboto", "Roboto Mono", "Ubuntu"],
            all.Families.Select(f => f.Family));
        Assert.Equal(["Roboto Mono"], mono.Families.Select(f => f.Family));
        Assert.Equal(2, limited.Families.Count);
        Assert.Equal(3, factory.GitHub.RequestsFor("api.github.com"));
        Assert.True(File.Exists(Path.Combine(factory.FontsPath, $"catalog-{Commit}.json")));
    }

    [Fact]
    public async Task Pinning_a_variable_family_describes_axes_features_and_fallback()
    {
        await using var factory = new CanvasFactory();
        var client = factory.CreateClient();

        var response = await client.PostAsJsonAsync("/api/fonts/google/pin", new PinFontRequest("roboto mono"), Ct);

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
        var body = (await response.Content.ReadFromJsonAsync<JsonElement>(Ct));
        Assert.Equal("monospace", body.GetProperty("genericFallback").GetString());
        var font = body.GetProperty("font");
        Assert.Equal("googlefonts", font.GetProperty("provider").GetString());
        Assert.Equal("Roboto Mono", font.GetProperty("key").GetString());
        Assert.Equal("Roboto Mono", font.GetProperty("displayName").GetString());
        Assert.Equal("roboto-mono", font.GetProperty("identifier").GetString());
        Assert.Equal(Commit, font.GetProperty("familyVersion").GetString());
        Assert.False(font.TryGetProperty("id", out _));
        Assert.False(font.TryGetProperty("order", out _));

        var axes = font.GetProperty("capabilities").GetProperty("axes").EnumerateArray().ToDictionary(a => a.GetProperty("tag").GetString()!);
        var wght = axes["wght"].GetProperty("domain");
        Assert.Equal(100, wght.GetProperty("min").GetDouble());
        Assert.Equal(700, wght.GetProperty("max").GetDouble());
        Assert.True(axes["wght"].TryGetProperty("default", out _));
        Assert.Equal([0.0, 1.0], axes["ital"].GetProperty("domain").GetProperty("values").EnumerateArray().Select(v => v.GetDouble()));
        Assert.NotEmpty(font.GetProperty("capabilities").GetProperty("features").EnumerateArray());
    }

    [Fact]
    public async Task Pinning_a_static_family_lists_its_weights()
    {
        await using var factory = new CanvasFactory();
        var client = factory.CreateClient();

        var body = (await (await client.PostAsJsonAsync("/api/fonts/google/pin", new PinFontRequest("Old Static"), Ct))
            .Content.ReadFromJsonAsync<JsonElement>(Ct));

        Assert.Equal("serif", body.GetProperty("genericFallback").GetString());
        var axes = body.GetProperty("font").GetProperty("capabilities").GetProperty("axes").EnumerateArray().ToList();
        var wght = Assert.Single(axes);
        Assert.Equal("wght", wght.GetProperty("tag").GetString());
        Assert.Equal([400.0, 700.0], wght.GetProperty("domain").GetProperty("values").EnumerateArray().Select(v => v.GetDouble()));
        Assert.Equal(400, wght.GetProperty("default").GetDouble());
    }

    [Fact]
    public async Task Pinning_an_unknown_or_invalid_family_is_refused()
    {
        await using var factory = new CanvasFactory();
        var client = factory.CreateClient();

        Assert.Equal(HttpStatusCode.NotFound,
            (await client.PostAsJsonAsync("/api/fonts/google/pin", new PinFontRequest("Nope Sans"), Ct)).StatusCode);
        Assert.Equal(HttpStatusCode.BadRequest,
            (await client.PostAsJsonAsync("/api/fonts/google/pin", new PinFontRequest("../etc"), Ct)).StatusCode);
    }

    [Fact]
    public async Task Manifest_lists_each_file_with_face_fixed_axes_and_hash()
    {
        await using var factory = new CanvasFactory();
        var client = factory.CreateClient();

        var manifest = (await client.GetFromJsonAsync<List<FontFileEntry>>(FilesUrl(Commit, "Roboto Mono"), Ct))!;

        Assert.Equal(2, manifest.Count);
        var upright = manifest.Single(f => f.File == "RobotoMono[wght].ttf");
        var italic = manifest.Single(f => f.File == "RobotoMono-Italic[wght].ttf");
        Assert.Equal($"googlefonts/Roboto Mono@{Commit}/RobotoMono[wght].ttf", upright.Face);
        Assert.Equal(0, upright.Fixed["ital"]);
        Assert.Equal(1, italic.Fixed["ital"]);
        Assert.DoesNotContain("wght", upright.Fixed.Keys);
        Assert.Equal(Sha(FakeGitHub.NormalBytes), upright.Sha256);
        Assert.Equal(Sha(FakeGitHub.ItalicBytes), italic.Sha256);

        var staticManifest = (await client.GetFromJsonAsync<List<FontFileEntry>>(FilesUrl(Commit, "Old Static"), Ct))!;
        Assert.Equal(400, staticManifest.Single(f => f.File == "OldStatic-Regular.ttf").Fixed["wght"]);
        Assert.DoesNotContain("ital", staticManifest[0].Fixed.Keys);
    }

    [Fact]
    public async Task File_download_returns_bytes_with_hash_header_and_stores_them_by_hash()
    {
        await using var factory = new CanvasFactory();
        var client = factory.CreateClient();
        var url = $"{FilesUrl(Commit, "Roboto Mono")}/{Uri.EscapeDataString("RobotoMono[wght].ttf")}";

        var response = await client.GetAsync(url, Ct);

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
        Assert.Equal("font/ttf", response.Content.Headers.ContentType!.MediaType);
        Assert.Equal(Sha(FakeGitHub.NormalBytes), response.Headers.GetValues("X-Font-SHA256").Single());
        Assert.Equal(FakeGitHub.NormalBytes, await response.Content.ReadAsByteArrayAsync(Ct));
        Assert.Equal(FakeGitHub.NormalBytes, File.ReadAllBytes(Path.Combine(factory.FontsPath, Sha(FakeGitHub.NormalBytes) + ".ttf")));
        var index = JsonSerializer.Deserialize<Dictionary<string, string>>(File.ReadAllText(Path.Combine(factory.FontsPath, "index.json")))!;
        Assert.Equal(Sha(FakeGitHub.NormalBytes), index[$"{Commit}/ofl/robotomono/RobotoMono[wght].ttf"]);

        var before = factory.GitHub.RequestsFor("raw.githubusercontent.com");
        await client.GetAsync(url, Ct);
        Assert.Equal(before, factory.GitHub.RequestsFor("raw.githubusercontent.com"));
    }

    [Fact]
    public async Task A_download_that_no_longer_matches_the_recorded_hash_is_refused()
    {
        await using var factory = new CanvasFactory();
        var client = factory.CreateClient();
        var url = $"{FilesUrl(Commit, "Roboto Mono")}/{Uri.EscapeDataString("RobotoMono[wght].ttf")}";
        Assert.Equal(HttpStatusCode.OK, (await client.GetAsync(url, Ct)).StatusCode);

        File.Delete(Path.Combine(factory.FontsPath, Sha(FakeGitHub.NormalBytes) + ".ttf"));
        factory.GitHub.ServeRaw("ofl/robotomono/RobotoMono[wght].ttf", FakeGitHub.ItalicBytes);
        var response = await client.GetAsync(url, Ct);

        Assert.Equal(HttpStatusCode.BadGateway, response.StatusCode);
        Assert.False(File.Exists(Path.Combine(factory.FontsPath, Sha(FakeGitHub.ItalicBytes) + ".ttf")));
    }

    [Fact]
    public async Task A_file_over_the_size_cap_is_refused()
    {
        await using var factory = new CanvasFactory();
        factory.GitHub.ServeRaw("ofl/oldstatic/OldStatic-Bold.ttf", new byte[FontFileStore.MaxFileBytes + 1]);
        var client = factory.CreateClient();

        var response = await client.GetAsync($"{FilesUrl(Commit, "Old Static")}/OldStatic-Bold.ttf", Ct);

        Assert.Equal(HttpStatusCode.BadGateway, response.StatusCode);
    }

    [Theory]
    [InlineData("main", "Roboto Mono", "RobotoMono[wght].ttf", HttpStatusCode.BadRequest)]
    [InlineData("CC39C4C2C304BC368C9B7C311EA2BCC6013299FE", "Roboto Mono", "RobotoMono[wght].ttf", HttpStatusCode.BadRequest)]
    [InlineData(Commit, "Roboto..Mono;", "RobotoMono[wght].ttf", HttpStatusCode.BadRequest)]
    [InlineData(Commit, "Roboto Mono", "Other.ttf", HttpStatusCode.NotFound)]
    [InlineData(Commit, "Roboto Mono", "METADATA.pb", HttpStatusCode.NotFound)]
    [InlineData(Commit, "Roboto Mono", "..%2FOldStatic-Bold.ttf", HttpStatusCode.NotFound)]
    [InlineData(Commit, "roboto mono", "RobotoMono[wght].ttf", HttpStatusCode.NotFound)]
    public async Task Invalid_versions_keys_and_files_are_refused(string version, string key, string file, HttpStatusCode expected)
    {
        await using var factory = new CanvasFactory();
        var client = factory.CreateClient();

        var response = await client.GetAsync($"/api/fonts/googlefonts/{version}/{Uri.EscapeDataString(key)}/files/{file}", Ct);

        Assert.Equal(expected, response.StatusCode);
        Assert.Equal(0, factory.GitHub.RequestsFor("raw.githubusercontent.com") * (expected == HttpStatusCode.BadRequest ? 1 : 0));
    }

    [Fact]
    public async Task Only_allowlisted_hosts_are_contacted()
    {
        await using var factory = new CanvasFactory();
        var client = factory.CreateClient();
        await client.GetAsync("/api/fonts/google/families", Ct);
        await client.PostAsJsonAsync("/api/fonts/google/pin", new PinFontRequest("Roboto Mono"), Ct);
        await client.GetAsync(FilesUrl(Commit, "Old Static"), Ct);

        Assert.NotEmpty(factory.GitHub.Requests);
        Assert.All(factory.GitHub.Requests, r => Assert.Contains(r.Host, GoogleFontsProvider.AllowedHosts));
        Assert.All(factory.GitHub.Requests, r => Assert.Equal(Uri.UriSchemeHttps, r.Scheme));
        Assert.Equal(["raw.githubusercontent.com", "api.github.com"], GoogleFontsProvider.AllowedHosts);
    }

    [Fact]
    public async Task An_upstream_failure_is_a_bad_gateway()
    {
        await using var factory = new CanvasFactory();
        factory.GitHub.Respond("ofl/robotomono/METADATA.pb", HttpStatusCode.InternalServerError);
        var client = factory.CreateClient();

        var response = await client.PostAsJsonAsync("/api/fonts/google/pin", new PinFontRequest("Roboto Mono"), Ct);

        Assert.Equal(HttpStatusCode.BadGateway, response.StatusCode);
    }

    private static JsonElement GoogleFont(Guid id, string key, string commit) => JsonSerializer.SerializeToElement(new
    {
        id,
        identifier = "roboto-mono",
        displayName = key,
        provider = "googlefonts",
        key,
        familyVersion = commit,
        capabilities = new { axes = Array.Empty<object>(), features = Array.Empty<string>() },
        order = "a1",
    });

    private static string FilesJson(IEnumerable<FontFileEntry> manifest, bool shifted = false) =>
        JsonSerializer.Serialize(manifest.Select(e => new { face = e.Face + (shifted ? "x" : ""), @fixed = e.Fixed }));

    private static async Task<List<FontFileEntry>> ManifestOf(HttpClient client, string commit, string key) =>
        (await client.GetFromJsonAsync<List<FontFileEntry>>(FilesUrl(commit, key), Ct))!;

    [Fact]
    public async Task Creating_a_google_font_registers_its_files_natively_before_the_op_applies()
    {
        await using var factory = new CanvasFactory();
        var client = factory.CreateClient();
        var commit = UniqueCommit();
        var document = (await (await client.PostAsJsonAsync("/api/documents", new CreateDocumentRequest("Fonts"), Ct))
            .Content.ReadFromJsonAsync<DocumentSummary>(Ct))!;
        var op = new OpInput(Guid.NewGuid(), "CreateFont",
            JsonSerializer.SerializeToElement(new { font = GoogleFont(Guid.NewGuid(), "Roboto Mono", commit) }));

        var appended = await client.PostAsJsonAsync($"/api/documents/{document.Id}/ops", new AppendOpsRequest([op]), Ct);

        var ack = (await appended.Content.ReadFromJsonAsync<AppendOpsResponse>(Ct))!.Acks.Single();
        Assert.Null(ack.Error);
        var manifest = await ManifestOf(client, commit, "Roboto Mono");
        Assert.Null(NativeFonts.RegisterFamily("googlefonts", "Roboto Mono", commit, FilesJson(manifest)));
        Assert.NotNull(NativeFonts.RegisterFamily("googlefonts", "Roboto Mono", commit, FilesJson(manifest, shifted: true)));
        Assert.NotNull(NativeFonts.RegisterFace(manifest[0].Face, FakeGitHub.ItalicBytes, Sha(FakeGitHub.ItalicBytes)));
        Assert.Null(NativeFonts.RegisterFace(manifest[0].Face, FakeGitHub.NormalBytes, manifest[0].Sha256));
    }

    [Fact]
    public async Task Opening_a_snapshot_with_a_google_font_registers_its_files_and_only_once()
    {
        await using var factory = new CanvasFactory();
        var client = factory.CreateClient();
        var commit = UniqueCommit();
        var snapshot = JsonSerializer.SerializeToElement(new
        {
            version = 25,
            modes = Array.Empty<object>(),
            tokens = Array.Empty<object>(),
            fonts = new[] { GoogleFont(Guid.NewGuid(), "Old Static", commit) },
            fontStacks = Array.Empty<object>(),
            nodes = Array.Empty<object>(),
        });
        var imported = await client.PostAsJsonAsync("/api/documents/import", new ImportDocumentRequest("Fonts", snapshot), Ct);
        Assert.Equal(HttpStatusCode.Created, imported.StatusCode);
        var document = (await imported.Content.ReadFromJsonAsync<DocumentSummary>(Ct))!;
        var node = new OpInput(Guid.NewGuid(), "CreateNode", JsonSerializer.SerializeToElement(new
        {
            node = new { id = Guid.NewGuid(), type = "frame", order = "V", name = "Card", w = 200.0, h = 100.0 },
        }));

        await client.PostAsJsonAsync($"/api/documents/{document.Id}/ops", new AppendOpsRequest([node]), Ct);

        var manifest = await ManifestOf(client, commit, "Old Static");
        Assert.NotNull(NativeFonts.RegisterFamily("googlefonts", "Old Static", commit, FilesJson(manifest, shifted: true)));
        Assert.Null(NativeFonts.RegisterFamily("googlefonts", "Old Static", commit, FilesJson(manifest)));

        var requests = factory.GitHub.Requests.Count;
        var again = new OpInput(Guid.NewGuid(), "CreateNode", JsonSerializer.SerializeToElement(new
        {
            node = new { id = Guid.NewGuid(), type = "frame", order = "W", name = "Card 2", w = 200.0, h = 100.0 },
        }));
        await client.PostAsJsonAsync($"/api/documents/{document.Id}/ops", new AppendOpsRequest([again]), Ct);
        Assert.Equal(requests, factory.GitHub.Requests.Count);
    }

    [Fact]
    public async Task A_font_that_cannot_be_fetched_does_not_fail_the_document_operation()
    {
        await using var factory = new CanvasFactory();
        factory.GitHub.Respond("ofl/robotomono/METADATA.pb", HttpStatusCode.InternalServerError);
        var client = factory.CreateClient();
        var document = (await (await client.PostAsJsonAsync("/api/documents", new CreateDocumentRequest("Fonts"), Ct))
            .Content.ReadFromJsonAsync<DocumentSummary>(Ct))!;
        var op = new OpInput(Guid.NewGuid(), "CreateFont",
            JsonSerializer.SerializeToElement(new { font = GoogleFont(Guid.NewGuid(), "Roboto Mono", UniqueCommit()) }));

        var appended = await client.PostAsJsonAsync($"/api/documents/{document.Id}/ops", new AppendOpsRequest([op]), Ct);

        Assert.Equal(HttpStatusCode.OK, appended.StatusCode);
        Assert.Null((await appended.Content.ReadFromJsonAsync<AppendOpsResponse>(Ct))!.Acks.Single().Error);
    }

    [Fact]
    public void Display_names_split_camel_case_and_digits()
    {
        Assert.Equal("Roboto Mono", GoogleFontsProvider.DisplayName("RobotoMono"));
        Assert.Equal("IBM Plex Sans", GoogleFontsProvider.DisplayName("IBMPlexSans"));
        Assert.Equal("Source Serif 4", GoogleFontsProvider.DisplayName("SourceSerif4"));
        Assert.Equal("Rubik 80s Fade", GoogleFontsProvider.DisplayName("Rubik80sFade"));
        Assert.Equal("Inter", GoogleFontsProvider.DisplayName("Inter"));
    }
}
