using System.Collections.Concurrent;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using Canvas.Server.VisualCore;
using Microsoft.Extensions.Options;

namespace Canvas.Server.Fonts;

public sealed class GoogleFontsOptions
{
    public const string Section = "GoogleFonts";

    /// <summary>The google/fonts commit new fonts are pinned to.</summary>
    public string Commit { get; set; } = "cc39c4c2c304bc368c9b7c311ea2bcc6013299fe";
}

/// <summary>google/fonts could not be reached or returned something unusable.</summary>
public sealed class GoogleFontsUnavailableException(string message, Exception? inner = null) : Exception(message, inner);

/// <summary>A family as the google/fonts repository lays it out at a commit.</summary>
public sealed record GoogleFontsFamily(string Root, string Slug, GoogleFontsMetadata Metadata)
{
    public string Directory => $"{Root}/{Slug}";
}

/// <summary>
/// Reads Google Fonts from the google/fonts GitHub repository at a pinned commit. Only the hosts in
/// <see cref="AllowedHosts"/> are contacted, so a commit's files never change under a document.
/// </summary>
public sealed partial class GoogleFontsProvider(
    IHttpClientFactory httpClients,
    FontFileStore store,
    IOptions<GoogleFontsOptions> options,
    ILogger<GoogleFontsProvider> logger)
{
    public const string ProviderName = "googlefonts";
    public const string HttpClientName = "googlefonts";

    public static readonly IReadOnlyList<string> AllowedHosts = ["raw.githubusercontent.com", "api.github.com"];
    public static readonly IReadOnlyList<string> LicenseRoots = ["ofl", "apache", "ufl"];

    private static readonly TimeSpan RequestTimeout = TimeSpan.FromSeconds(30);

    private readonly ConcurrentDictionary<string, GoogleFontsFamily?> families = new();
    private readonly ConcurrentDictionary<string, IReadOnlyList<string>> catalogs = new();
    private readonly SemaphoreSlim catalogGate = new(1, 1);

    /// <summary>The commit used when a request names none.</summary>
    public string DefaultCommit => options.Value.Commit;

    [GeneratedRegex("^[0-9a-f]{40}$")]
    private static partial Regex CommitPattern();

    [GeneratedRegex("^[A-Za-z0-9][A-Za-z0-9 '.,&+-]{0,99}$")]
    private static partial Regex KeyPattern();

    public static bool IsValidCommit(string? commit) => commit is not null && CommitPattern().IsMatch(commit);

    public static bool IsValidKey(string? key) => key is not null && KeyPattern().IsMatch(key);

    /// <summary>Family names at <paramref name="commit"/>, sorted.</summary>
    /// <exception cref="GoogleFontsUnavailableException">GitHub could not be read.</exception>
    public async Task<IReadOnlyList<string>> GetCatalogAsync(string commit, CancellationToken ct)
    {
        if (catalogs.TryGetValue(commit, out var cached))
        {
            return cached;
        }
        await catalogGate.WaitAsync(ct);
        try
        {
            if (catalogs.TryGetValue(commit, out cached))
            {
                return cached;
            }
            var path = Path.Combine(store.Root, $"catalog-{commit}.json");
            if (File.Exists(path))
            {
                try
                {
                    var names = JsonSerializer.Deserialize<List<string>>(await File.ReadAllTextAsync(path, ct));
                    if (names is not null)
                    {
                        return catalogs[commit] = names;
                    }
                }
                catch (JsonException)
                {
                }
            }

            var catalog = await FetchCatalogAsync(commit, ct);
            Directory.CreateDirectory(store.Root);
            await File.WriteAllTextAsync(path, JsonSerializer.Serialize(catalog), ct);
            return catalogs[commit] = catalog;
        }
        finally
        {
            catalogGate.Release();
        }
    }

    /// <summary>Builds the font a document stores for <paramref name="family"/> at <paramref name="commit"/>, or null if there is none.</summary>
    public async Task<(PinnedFont Font, string GenericFallback)?> PinAsync(string family, string commit, CancellationToken ct)
    {
        if (await FindFamilyAsync(commit, Slug(family), ct) is not { } found)
        {
            return null;
        }
        var metadata = found.Metadata;
        var reference = ReferenceFile(metadata);
        var (_, bytes) = await GetFileBytesAsync(commit, found, reference.Filename, ct);
        var offered = NativeFonts.Capabilities(bytes);

        var hasNormal = metadata.Files.Any(f => !IsItalic(f));
        var hasItalic = metadata.Files.Any(IsItalic);
        var axes = new List<FontAxisCapability>();

        if (metadata.IsVariable)
        {
            foreach (var axis in metadata.Axes)
            {
                axes.Add(new FontAxisCapability(axis.Tag, new FontAxisDomain(axis.Min, axis.Max, null),
                    DefaultOf(axis, offered)));
            }
        }
        else
        {
            var weights = metadata.Files.Select(f => f.Weight).Distinct().Order().ToList();
            axes.Add(new FontAxisCapability("wght", new FontAxisDomain(null, null, weights), reference.Weight));
        }
        if (hasItalic && !axes.Any(a => a.Tag == "ital"))
        {
            axes.Add(hasNormal
                ? new FontAxisCapability("ital", new FontAxisDomain(null, null, [0, 1]), 0)
                : new FontAxisCapability("ital", new FontAxisDomain(null, null, [1]), 1));
        }

        var features = new List<string>();
        if (offered is { } caps && caps.TryGetProperty("features", out var list) && list.ValueKind == JsonValueKind.Array)
        {
            features.AddRange(list.EnumerateArray().Where(f => f.ValueKind == JsonValueKind.String).Select(f => f.GetString()!));
        }

        var font = new PinnedFont(Kebab(metadata.Name), metadata.Name, ProviderName, metadata.Name, commit,
            new FontCapabilitiesInfo(axes, features));
        var fallback = metadata.Category.ToUpperInvariant() switch
        {
            "SERIF" => "serif",
            "MONOSPACE" => "monospace",
            _ => "sans-serif",
        };
        return (font, fallback);
    }

    /// <summary>The files of a family at a commit with their hashes, downloading those the store lacks. Null if the family does not exist.</summary>
    public async Task<IReadOnlyList<FontFileEntry>?> GetManifestAsync(string commit, string key, CancellationToken ct)
    {
        if (await FindExactAsync(commit, key, ct) is not { } found)
        {
            return null;
        }
        var metadata = found.Metadata;
        var hasItalic = metadata.Files.Any(IsItalic);
        var entries = new List<FontFileEntry>();
        foreach (var file in metadata.Files)
        {
            var fixedAxes = new Dictionary<string, double>();
            if (hasItalic)
            {
                fixedAxes["ital"] = IsItalic(file) ? 1 : 0;
            }
            if (!metadata.IsVariable)
            {
                fixedAxes["wght"] = file.Weight;
            }
            var (sha256, _) = await GetFileBytesAsync(commit, found, file.Filename, ct);
            entries.Add(new FontFileEntry(FaceId(metadata.Name, commit, file.Filename), fixedAxes, sha256, file.Filename));
        }
        return entries;
    }

    /// <summary>One file listed in the family's METADATA.pb, or null if the family or the file is not there.</summary>
    public async Task<(string Sha256, byte[] Bytes)?> GetFileAsync(string commit, string key, string file, CancellationToken ct)
    {
        if (await FindExactAsync(commit, key, ct) is not { } found ||
            found.Metadata.Files.All(f => f.Filename != file))
        {
            return null;
        }
        return await GetFileBytesAsync(commit, found, file, ct);
    }

    public static string FaceId(string key, string commit, string filename) => $"{ProviderName}/{key}@{commit}/{filename}";

    internal static string Slug(string family) =>
        new(family.Where(char.IsLetterOrDigit).Select(char.ToLowerInvariant).ToArray());

    internal static string Kebab(string name)
    {
        var text = new StringBuilder();
        foreach (var c in name)
        {
            if (char.IsLetterOrDigit(c))
            {
                text.Append(char.ToLowerInvariant(c));
            }
            else if (text.Length > 0 && text[^1] != '-')
            {
                text.Append('-');
            }
        }
        return text.ToString().TrimEnd('-');
    }

    /// <summary>Turns the file name prefix of a family (<c>RobotoMono</c>) into its display name (<c>Roboto Mono</c>).</summary>
    public static string DisplayName(string prefix)
    {
        var text = new StringBuilder();
        for (var i = 0; i < prefix.Length; i++)
        {
            var c = prefix[i];
            if (i > 0)
            {
                var previous = prefix[i - 1];
                var next = i + 1 < prefix.Length ? prefix[i + 1] : '\0';
                var boundary =
                    (char.IsUpper(c) && (char.IsLower(previous) || char.IsDigit(previous))) ||
                    (char.IsUpper(c) && char.IsUpper(previous) && char.IsLower(next)) ||
                    (char.IsDigit(c) && char.IsLetter(previous));
                if (boundary)
                {
                    text.Append(' ');
                }
            }
            text.Append(c);
        }
        return text.ToString();
    }

    private static bool IsItalic(GoogleFontsFile file) => file.Style.Equals("italic", StringComparison.OrdinalIgnoreCase);

    private static GoogleFontsFile ReferenceFile(GoogleFontsMetadata metadata)
    {
        var pool = metadata.Files.Where(f => !IsItalic(f)).ToList();
        return (pool.Count > 0 ? pool : metadata.Files).MinBy(f => Math.Abs(f.Weight - 400))!;
    }

    private static double DefaultOf(GoogleFontsAxis axis, JsonElement? offered)
    {
        if (offered is { } caps && caps.TryGetProperty("axes", out var list) && list.ValueKind == JsonValueKind.Array)
        {
            foreach (var entry in list.EnumerateArray())
            {
                if (entry.TryGetProperty("tag", out var tag) && tag.GetString() == axis.Tag &&
                    entry.TryGetProperty("default", out var value) && value.TryGetDouble(out var number))
                {
                    return Math.Clamp(number, axis.Min, axis.Max);
                }
            }
        }
        return axis.Tag == "wght" ? Math.Clamp(400, axis.Min, axis.Max) : axis.Min;
    }

    private async Task<GoogleFontsFamily?> FindExactAsync(string commit, string key, CancellationToken ct) =>
        await FindFamilyAsync(commit, Slug(key), ct) is { } found && found.Metadata.Name == key ? found : null;

    private async Task<GoogleFontsFamily?> FindFamilyAsync(string commit, string slug, CancellationToken ct)
    {
        if (slug.Length == 0)
        {
            return null;
        }
        var cacheKey = $"{commit}/{slug}";
        if (families.TryGetValue(cacheKey, out var cached))
        {
            return cached;
        }

        foreach (var root in LicenseRoots)
        {
            var text = await ReadMetadataAsync(commit, root, slug, ct);
            if (text is null)
            {
                continue;
            }
            try
            {
                return families[cacheKey] = new GoogleFontsFamily(root, slug, GoogleFontsMetadata.Parse(text));
            }
            catch (FormatException bad)
            {
                throw new GoogleFontsUnavailableException($"METADATA.pb of {root}/{slug} could not be read: {bad.Message}", bad);
            }
        }
        return families[cacheKey] = null;
    }

    private async Task<string?> ReadMetadataAsync(string commit, string root, string slug, CancellationToken ct)
    {
        var cache = Path.Combine(store.Root, "metadata", commit, root, slug + ".pb");
        if (File.Exists(cache))
        {
            return await File.ReadAllTextAsync(cache, ct);
        }
        var bytes = await GetAsync(RawUri(commit, $"{root}/{slug}/METADATA.pb"), allowNotFound: true, ct);
        if (bytes is null)
        {
            return null;
        }
        var text = Encoding.UTF8.GetString(bytes);
        Directory.CreateDirectory(Path.GetDirectoryName(cache)!);
        await File.WriteAllTextAsync(cache, text, ct);
        return text;
    }

    private Task<(string Sha256, byte[] Bytes)> GetFileBytesAsync(string commit, GoogleFontsFamily family, string filename, CancellationToken ct) =>
        store.GetOrFetchAsync(commit, $"{family.Directory}/{filename}",
            async token => (await GetAsync(RawUri(commit, $"{family.Directory}/{filename}"), allowNotFound: false, token))!, ct);

    private async Task<IReadOnlyList<string>> FetchCatalogAsync(string commit, CancellationToken ct)
    {
        var names = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var root in LicenseRoots)
        {
            var uri = new Uri($"https://api.github.com/repos/google/fonts/git/trees/{commit}:{root}?recursive=1");
            var bytes = await GetAsync(uri, allowNotFound: false, ct);
            using var document = JsonDocument.Parse(bytes!);
            if (document.RootElement.TryGetProperty("truncated", out var truncated) && truncated.ValueKind == JsonValueKind.True)
            {
                logger.LogWarning("The google/fonts tree of {Root} at {Commit} was truncated by GitHub", root, commit);
            }
            var prefixes = new Dictionary<string, List<string>>();
            foreach (var entry in document.RootElement.GetProperty("tree").EnumerateArray())
            {
                var path = entry.GetProperty("path").GetString()!;
                var parts = path.Split('/');
                if (parts.Length != 2 || !parts[1].EndsWith(".ttf", StringComparison.OrdinalIgnoreCase))
                {
                    continue;
                }
                var cut = parts[1].IndexOfAny(['[', '-', '.']);
                if (!prefixes.TryGetValue(parts[0], out var list))
                {
                    prefixes[parts[0]] = list = [];
                }
                list.Add(parts[1][..cut]);
            }
            foreach (var prefix in prefixes.Values)
            {
                var best = prefix.GroupBy(p => p).OrderByDescending(g => g.Count()).ThenBy(g => g.Key.Length).First().Key;
                names.Add(DisplayName(best));
            }
        }
        return names.ToList();
    }

    private static Uri RawUri(string commit, string path) =>
        new($"https://raw.githubusercontent.com/google/fonts/{commit}/{string.Join('/', path.Split('/').Select(Uri.EscapeDataString))}");

    /// <summary>Reads at most <see cref="FontFileStore.MaxFileBytes"/> bytes from an allowed host. Null on 404 when allowed.</summary>
    private async Task<byte[]?> GetAsync(Uri uri, bool allowNotFound, CancellationToken ct)
    {
        if (uri.Scheme != Uri.UriSchemeHttps || !AllowedHosts.Contains(uri.Host))
        {
            throw new GoogleFontsUnavailableException($"{uri.Host} is not an allowed font host.");
        }

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
                throw new GoogleFontsUnavailableException($"{uri.Host} answered {(int)response.StatusCode} for {uri.AbsolutePath}.");
            }
            if (response.Content.Headers.ContentLength > FontFileStore.MaxFileBytes)
            {
                throw new FontFileException($"{uri.AbsolutePath} is larger than {FontFileStore.MaxFileBytes} bytes.");
            }

            await using var body = await response.Content.ReadAsStreamAsync(timeout.Token);
            using var buffer = new MemoryStream();
            var chunk = new byte[81920];
            int read;
            while ((read = await body.ReadAsync(chunk, timeout.Token)) > 0)
            {
                if (buffer.Length + read > FontFileStore.MaxFileBytes)
                {
                    throw new FontFileException($"{uri.AbsolutePath} is larger than {FontFileStore.MaxFileBytes} bytes.");
                }
                buffer.Write(chunk, 0, read);
            }
            return buffer.ToArray();
        }
        catch (HttpRequestException e)
        {
            throw new GoogleFontsUnavailableException($"{uri.Host} could not be reached.", e);
        }
        catch (OperationCanceledException) when (!ct.IsCancellationRequested)
        {
            throw new GoogleFontsUnavailableException($"{uri.Host} timed out.");
        }
    }
}
