using System.Collections.Concurrent;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using Canvas.Server.VisualCore;
using Microsoft.Extensions.Options;

namespace Canvas.Server.Icons;

public sealed class MaterialSymbolsOptions
{
    public const string Section = "MaterialSymbols";

    /// <summary>
    /// The google/material-design-icons commit icons are read from. It has to be the commit the built-in set of visualcore is
    /// pinned to, so the server refuses to start with another one.
    /// </summary>
    public string Commit { get; set; } = NativeIcons.MaterialSymbolsCommit;
}

/// <summary>google/material-design-icons could not be reached or returned something unusable.</summary>
public sealed class IconsUnavailableException(string message, Exception? inner = null) : Exception(message, inner);

/// <summary>
/// Reads Material Symbols from the google/material-design-icons GitHub repository at a pinned commit. The glyph
/// inventory is the folder list of <c>symbols/web</c> (the <c>variablefont</c> codepoints file also names about 200 retired
/// aliases that have no SVG), and each SVG is <c>symbols/web/&lt;glyph&gt;/materialsymbols&lt;style&gt;/&lt;glyph&gt;[_wght&lt;n&gt;][fill1]_&lt;opsz&gt;px.svg</c>.
/// Only the hosts in <see cref="AllowedHosts"/> are contacted, and files are cached in the <see cref="IconFileStore"/>.
/// </summary>
public sealed partial class MaterialSymbolsProvider(
    IHttpClientFactory httpClients,
    IconFileStore store,
    IOptions<MaterialSymbolsOptions> options,
    ILogger<MaterialSymbolsProvider> logger)
{
    public const string SetName = "material-symbols";
    public const string HttpClientName = "materialsymbols";
    public static readonly IReadOnlyList<string> AllowedHosts = ["raw.githubusercontent.com", "api.github.com"];

    private static readonly TimeSpan RequestTimeout = TimeSpan.FromSeconds(30);

    private readonly ConcurrentDictionary<string, GlyphInventory> inventories = new();
    private readonly ConcurrentDictionary<string, byte> missing = new();
    private readonly SemaphoreSlim inventoryGate = new(1, 1);

    /// <summary>The commit used when a request names none.</summary>
    public string DefaultCommit => options.Value.Commit;

    [GeneratedRegex("^[0-9a-f]{40}$")]
    private static partial Regex CommitPattern();

    [GeneratedRegex("^[a-z0-9_]{1,64}$")]
    private static partial Regex GlyphPattern();

    public static bool IsValidCommit(string? commit) => commit is not null && CommitPattern().IsMatch(commit);

    public static bool IsValidGlyph(string? glyph) => glyph is not null && GlyphPattern().IsMatch(glyph);

    /// <summary>Glyph names whose name starts with, then contains, <paramref name="query"/>, each group in alphabetical order, one page.</summary>
    /// <exception cref="IconsUnavailableException">GitHub could not be read.</exception>
    public async Task<IconSearchResponse> SearchAsync(string? query, int offset, int limit, string commit, CancellationToken ct)
    {
        var inventory = await GetInventoryAsync(commit, ct);
        var needle = Normalize(query);
        IEnumerable<string> matches = inventory.Sorted;
        if (needle.Length > 0)
        {
            matches = inventory.Sorted.Where(g => g.StartsWith(needle, StringComparison.Ordinal))
                .Concat(inventory.Sorted.Where(g => !g.StartsWith(needle, StringComparison.Ordinal) && g.Contains(needle, StringComparison.Ordinal)));
        }
        var all = matches.ToList();
        var page = all.Skip(offset).Take(limit).Select(g => new IconGlyph(g)).ToList();
        return new IconSearchResponse(commit, all.Count, offset, page);
    }

    /// <summary>Whether the source has <paramref name="glyph"/> at all.</summary>
    public async Task<bool> HasGlyphAsync(string glyph, string commit, CancellationToken ct) =>
        (await GetInventoryAsync(commit, ct)).Names.Contains(glyph);

    /// <summary>
    /// The SVG of <paramref name="glyph"/> at <paramref name="variant"/>, downloading it when the store lacks it. Null when
    /// the source has no such glyph or no SVG for those axis values.
    /// </summary>
    /// <exception cref="IconsUnavailableException">GitHub could not be read.</exception>
    public async Task<string?> GetSvgAsync(string glyph, IconVariant variant, string commit, CancellationToken ct)
    {
        if (!IsValidGlyph(glyph) || !variant.IsOffered || !await HasGlyphAsync(glyph, commit, ct))
        {
            return null;
        }
        var relative = $"{variant.Style}/{variant.FileName(glyph)}";
        var cacheKey = $"{commit}/{glyph}/{relative}";
        if (missing.ContainsKey(cacheKey))
        {
            return null;
        }
        if (await store.ReadAsync(SetName, commit, relative, ct) is { } cached)
        {
            return cached;
        }
        var remote = $"symbols/web/{glyph}/materialsymbols{variant.Style}/{variant.FileName(glyph)}";
        var bytes = await GetAsync(RawUri(commit, remote), allowNotFound: true, ct);
        if (bytes is null)
        {
            missing[cacheKey] = 0;
            return null;
        }
        var svg = Encoding.UTF8.GetString(bytes);
        if (!svg.TrimStart().StartsWith("<svg", StringComparison.Ordinal))
        {
            throw new IconsUnavailableException($"{remote} is not an SVG.");
        }
        await store.WriteAsync(SetName, commit, relative, svg, ct);
        return svg;
    }

    private static string Normalize(string? query) =>
        string.Join('_', (query ?? "").Trim().ToLowerInvariant().Split([' ', '_', '-'], StringSplitOptions.RemoveEmptyEntries));

    private async Task<GlyphInventory> GetInventoryAsync(string commit, CancellationToken ct)
    {
        if (inventories.TryGetValue(commit, out var cached))
        {
            return cached;
        }
        await inventoryGate.WaitAsync(ct);
        try
        {
            if (inventories.TryGetValue(commit, out cached))
            {
                return cached;
            }
            var text = await store.ReadAsync(SetName, commit, "glyphs.txt", ct);
            if (text is null)
            {
                var uri = new Uri($"https://api.github.com/repos/google/material-design-icons/git/trees/{commit}:symbols/web");
                var bytes = await GetAsync(uri, allowNotFound: false, ct);
                JsonDocument tree;
                try
                {
                    tree = JsonDocument.Parse(bytes!);
                }
                catch (JsonException e)
                {
                    throw new IconsUnavailableException("The Material Symbols glyph list is not JSON.", e);
                }
                using var _ = tree;
                if (tree.RootElement.TryGetProperty("truncated", out var truncated) && truncated.ValueKind == JsonValueKind.True)
                {
                    throw new IconsUnavailableException("GitHub truncated the Material Symbols glyph list.");
                }
                var names = tree.RootElement.GetProperty("tree").EnumerateArray()
                    .Where(entry => entry.GetProperty("type").GetString() == "tree")
                    .Select(entry => entry.GetProperty("path").GetString())
                    .Where(IsValidGlyph)
                    .Select(glyph => glyph!)
                    .Distinct()
                    .Order(StringComparer.Ordinal);
                text = string.Join('\n', names);
                if (text.Length == 0)
                {
                    throw new IconsUnavailableException("The Material Symbols glyph inventory is empty.");
                }
                await store.WriteAsync(SetName, commit, "glyphs.txt", text, ct);
            }
            var sorted = text.Split('\n', StringSplitOptions.RemoveEmptyEntries);
            return inventories[commit] = new GlyphInventory(sorted, sorted.ToHashSet(StringComparer.Ordinal));
        }
        finally
        {
            inventoryGate.Release();
        }
    }

    private static Uri RawUri(string commit, string path) =>
        new($"https://raw.githubusercontent.com/google/material-design-icons/{commit}/{string.Join('/', path.Split('/').Select(Uri.EscapeDataString))}");

    /// <summary>Reads at most <see cref="IconFileStore.MaxFileBytes"/> bytes from an allowed host. Null on 404 when allowed.</summary>
    private async Task<byte[]?> GetAsync(Uri uri, bool allowNotFound, CancellationToken ct)
    {
        if (uri.Scheme != Uri.UriSchemeHttps || !AllowedHosts.Contains(uri.Host))
        {
            throw new IconsUnavailableException($"{uri.Host} is not an allowed icon host.");
        }
        var limit = uri.Host == "api.github.com" ? 1024 * 1024 : IconFileStore.MaxFileBytes;

        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(ct);
        timeout.CancelAfter(RequestTimeout);
        try
        {
            using var request = new HttpRequestMessage(HttpMethod.Get, uri);
            request.Headers.UserAgent.ParseAdd("Canvas-Server");
            request.Headers.Accept.ParseAdd("application/vnd.github+json, */*");
            using var response = await httpClients.CreateClient(HttpClientName)
                .SendAsync(request, HttpCompletionOption.ResponseHeadersRead, timeout.Token);
            if (response.StatusCode == System.Net.HttpStatusCode.NotFound && allowNotFound)
            {
                return null;
            }
            if (!response.IsSuccessStatusCode)
            {
                throw new IconsUnavailableException($"{uri.Host} answered {(int)response.StatusCode} for {uri.AbsolutePath}.");
            }
            if (response.Content.Headers.ContentLength > limit)
            {
                throw new IconsUnavailableException($"{uri.AbsolutePath} is larger than {limit} bytes.");
            }

            await using var body = await response.Content.ReadAsStreamAsync(timeout.Token);
            using var buffer = new MemoryStream();
            var chunk = new byte[16384];
            int read;
            while ((read = await body.ReadAsync(chunk, timeout.Token)) > 0)
            {
                if (buffer.Length + read > limit)
                {
                    throw new IconsUnavailableException($"{uri.AbsolutePath} is larger than {limit} bytes.");
                }
                buffer.Write(chunk, 0, read);
            }
            return buffer.ToArray();
        }
        catch (HttpRequestException e)
        {
            logger.LogWarning(e, "{Host} could not be reached for {Path}", uri.Host, uri.AbsolutePath);
            throw new IconsUnavailableException($"{uri.Host} could not be reached.", e);
        }
        catch (OperationCanceledException) when (!ct.IsCancellationRequested)
        {
            throw new IconsUnavailableException($"{uri.Host} timed out.");
        }
    }

    private sealed record GlyphInventory(string[] Sorted, HashSet<string> Names);
}
