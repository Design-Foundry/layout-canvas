using Canvas.Server.Storage;
using Microsoft.Extensions.Options;

namespace Canvas.Server.Icons;

/// <summary>
/// Icon files on disk under <c>&lt;root&gt;/&lt;set&gt;/&lt;commit&gt;/</c>, laid out as the source repository is.
/// A commit's files never change, so a cached file is served as it is. The folder is
/// <c>Storage:Icons</c>, or <c>icons</c> next to the fonts folder when that is not set.
/// </summary>
public sealed class IconFileStore
{
    public const int MaxFileBytes = 64 * 1024;

    private readonly string root;

    public IconFileStore(IOptions<StorageOptions> options, IHostEnvironment environment)
    {
        var storage = options.Value;
        root = storage.Icons is { Length: > 0 } icons
            ? Path.GetFullPath(icons, environment.ContentRootPath)
            : Path.Combine(Path.GetDirectoryName(Path.GetFullPath(storage.Fonts, environment.ContentRootPath).TrimEnd(Path.DirectorySeparatorChar))!, "icons");
    }

    /// <summary>The folder holding the store.</summary>
    public string Root => root;

    /// <summary>The cached text at <paramref name="relativePath"/> (segments already validated by the caller), or null.</summary>
    public async Task<string?> ReadAsync(string set, string commit, string relativePath, CancellationToken ct)
    {
        var path = PathOf(set, commit, relativePath);
        return File.Exists(path) ? await File.ReadAllTextAsync(path, ct) : null;
    }

    public async Task WriteAsync(string set, string commit, string relativePath, string text, CancellationToken ct)
    {
        var path = PathOf(set, commit, relativePath);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var temp = path + $".{Guid.NewGuid():N}.tmp";
        await File.WriteAllTextAsync(temp, text, ct);
        File.Move(temp, path, overwrite: true);
    }

    private string PathOf(string set, string commit, string relativePath)
    {
        var path = Path.GetFullPath(Path.Combine(root, set, commit, relativePath));
        if (!path.StartsWith(root + Path.DirectorySeparatorChar, StringComparison.Ordinal))
        {
            throw new InvalidOperationException("The icon path leaves the icon store.");
        }
        return path;
    }
}
