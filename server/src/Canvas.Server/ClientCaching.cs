using System.Text.RegularExpressions;

namespace Canvas.Server;

/// <summary>
/// Cache headers for the packaged client: files whose names carry a content hash are cached for good,
/// everything else (index.html, the WASM module, fonts) is revalidated on every load.
/// </summary>
public static partial class ClientCaching
{
    public const string Immutable = "public, max-age=31536000, immutable";
    public const string Revalidate = "no-cache";

    public static void Apply(HttpResponse response, string fileName) =>
        response.Headers.CacheControl = IsHashed(fileName) ? Immutable : Revalidate;

    public static bool IsHashed(string fileName) => HashedName().IsMatch(fileName);

    [GeneratedRegex(@"-[A-Za-z0-9]{8}\.(js|css)$")]
    private static partial Regex HashedName();
}
