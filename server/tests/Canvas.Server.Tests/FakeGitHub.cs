using System.Net;
using System.Text;
using System.Text.Json;

namespace Canvas.Server.Tests;

public sealed class FakeHttpClientFactory(HttpMessageHandler handler) : IHttpClientFactory
{
    public HttpClient CreateClient(string name) => new(handler, disposeHandler: false);
}

/// <summary>Serves google/fonts fixtures in place of GitHub and records every request.</summary>
public sealed class FakeGitHub : HttpMessageHandler
{
    public const string Commit = "cc39c4c2c304bc368c9b7c311ea2bcc6013299fe";

    public static byte[] NormalBytes { get; } = File.ReadAllBytes(FontAsset("JetBrainsMono.ttf"));
    public static byte[] ItalicBytes { get; } = File.ReadAllBytes(FontAsset("SourceSerif4.ttf"));

    public const string RobotoMonoMetadata = """
        name: "Roboto Mono"
        designer: "Christian Robertson"
        license: "OFL"
        category: "MONOSPACE"
        fonts {
          name: "Roboto Mono"
          style: "normal"
          weight: 400
          filename: "RobotoMono[wght].ttf"
          post_script_name: "RobotoMono-Regular"
        }
        fonts {
          name: "Roboto Mono"
          style: "italic"
          weight: 400
          filename: "RobotoMono-Italic[wght].ttf"
        }
        axes {
          tag: "wght"
          min_value: 100.0
          max_value: 700.0
        }
        source {
          commit: "0123456789abcdef"
        }
        """;

    public const string OldStaticMetadata = """
        name: "Old Static"
        category: "SERIF"
        fonts {
          style: "normal"
          weight: 700
          filename: "OldStatic-Bold.ttf"
        }
        fonts {
          style: "normal"
          weight: 400
          filename: "OldStatic-Regular.ttf"
        }
        """;

    /// <summary>The SVG the fake serves for <paramref name="glyph"/> at <paramref name="file"/>, which tells apart the files.</summary>
    public static string IconSvg(string glyph, string file) =>
        $"""<svg xmlns="http://www.w3.org/2000/svg" height="24" viewBox="0 -960 960 960" width="24"><path d="M0-960H480V0H0Z"/><!-- {glyph} {file} --></svg>""";

    public void ServeIcon(string glyph, string style, string file) =>
        ServeRaw($"symbols/web/{glyph}/materialsymbols{style}/{file}", Encoding.UTF8.GetBytes(IconSvg(glyph, $"{style}/{file}")));

    private readonly Dictionary<string, Func<HttpResponseMessage>> routes = new();

    public List<Uri> Requests { get; } = [];

    public static FakeGitHub WithFixtures()
    {
        var github = new FakeGitHub();
        github.ServeRaw("ofl/robotomono/METADATA.pb", Encoding.UTF8.GetBytes(RobotoMonoMetadata));
        github.ServeRaw("ofl/robotomono/RobotoMono[wght].ttf", NormalBytes);
        github.ServeRaw("ofl/robotomono/RobotoMono-Italic[wght].ttf", ItalicBytes);
        github.ServeRaw("ofl/oldstatic/METADATA.pb", Encoding.UTF8.GetBytes(OldStaticMetadata));
        github.ServeRaw("ofl/oldstatic/OldStatic-Regular.ttf", NormalBytes);
        github.ServeRaw("ofl/oldstatic/OldStatic-Bold.ttf", ItalicBytes);
        github.ServeTree("ofl",
            "robotomono/RobotoMono[wght].ttf", "robotomono/RobotoMono-Italic[wght].ttf", "robotomono/METADATA.pb",
            "opensans/OpenSans[wdth,wght].ttf", "ibmplexsans/IBMPlexSans-Regular.ttf", "oldstatic/OldStatic-Regular.ttf");
        github.ServeTree("apache", "roboto/Roboto[wdth,wght].ttf");
        github.ServeTree("ufl", "ubuntu/Ubuntu-Regular.ttf");
        github.ServeIconTree("10k", "arrow_back", "arrow_forward", "arrow_upward", "home", "keyboard_arrow_left", "no_svg_glyph", "search", "settings");
        foreach (var glyph in new[] { "10k", "arrow_back", "arrow_forward", "arrow_upward", "home", "keyboard_arrow_left", "search", "settings" })
        {
            github.ServeIcon(glyph, "outlined", $"{glyph}_24px.svg");
            github.ServeIcon(glyph, "rounded", $"{glyph}_wght500fill1_24px.svg");
            github.ServeIcon(glyph, "sharp", $"{glyph}_fill1_48px.svg");
            github.ServeIcon(glyph, "outlined", $"{glyph}_wght100_20px.svg");
        }
        return github;
    }

    /// <summary>Serves the folder list of <c>symbols/web</c>, the glyph inventory.</summary>
    public void ServeIconTree(params string[] glyphs) =>
        routes["api.github.com|symbols/web"] = () => new HttpResponseMessage(HttpStatusCode.OK)
        {
            Content = new StringContent(JsonSerializer.Serialize(new
            {
                truncated = false,
                tree = glyphs.Select(g => new { path = g, type = "tree" }).Append(new { path = "README.md", type = "blob" }),
            })),
        };

    public void ServeRaw(string path, byte[] bytes) =>
        routes[$"raw.githubusercontent.com|{path}"] = () => new HttpResponseMessage(HttpStatusCode.OK) { Content = new ByteArrayContent(bytes) };

    public void ServeTree(string root, params string[] paths) =>
        routes[$"api.github.com|{root}"] = () => new HttpResponseMessage(HttpStatusCode.OK)
        {
            Content = new StringContent(JsonSerializer.Serialize(new
            {
                truncated = false,
                tree = paths.Select(p => new { path = p, type = "blob" }),
            })),
        };

    public void Respond(string path, HttpStatusCode status) =>
        routes[$"raw.githubusercontent.com|{path}"] = () => new HttpResponseMessage(status);

    public int RequestsFor(string host) => Requests.Count(r => r.Host == host);

    protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancellationToken)
    {
        var uri = request.RequestUri!;
        Requests.Add(uri);
        var path = Uri.UnescapeDataString(uri.AbsolutePath);
        string? key = null;
        if (uri.Host == "raw.githubusercontent.com")
        {
            var parts = path.TrimStart('/').Split('/', 4);
            key = $"{uri.Host}|{parts[3]}";
        }
        else if (uri.Host == "api.github.com")
        {
            key = $"{uri.Host}|{path[(path.LastIndexOf(':') + 1)..]}";
        }
        return Task.FromResult(key is not null && routes.TryGetValue(key, out var response)
            ? response()
            : new HttpResponseMessage(HttpStatusCode.NotFound));
    }

    private static string FontAsset(string name)
    {
        for (var directory = new DirectoryInfo(AppContext.BaseDirectory); directory is not null; directory = directory.Parent)
        {
            var candidate = Path.Combine(directory.FullName, "visualcore", "assets", "fonts", name);
            if (File.Exists(candidate))
            {
                return candidate;
            }
        }
        throw new FileNotFoundException(name);
    }
}
