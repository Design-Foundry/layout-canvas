using System.Security.Cryptography;
using System.Text.Json;
using Canvas.Server.Storage;
using Microsoft.Extensions.Options;

namespace Canvas.Server.Fonts;

/// <summary>The font file could not be stored or does not match what the store recorded for it.</summary>
public sealed class FontFileException(string message) : Exception(message);

/// <summary>
/// Font files on disk as <c>&lt;sha256&gt;.ttf</c>, with an index from <c>(commit, path)</c> to the hash
/// the server saw the first time. A file that later hashes differently is refused.
/// </summary>
public sealed class FontFileStore
{
    public const int MaxFileBytes = 32 * 1024 * 1024;

    private readonly SemaphoreSlim gate = new(1, 1);
    private readonly string root;
    private readonly string indexPath;
    private Dictionary<string, string>? index;

    public FontFileStore(IOptions<StorageOptions> options, IHostEnvironment environment)
    {
        root = Path.GetFullPath(options.Value.Fonts, environment.ContentRootPath);
        indexPath = Path.Combine(root, "index.json");
    }

    /// <summary>The folder holding the store, also used for cached listings.</summary>
    public string Root => root;

    /// <summary>Returns the file's SHA-256 and bytes, fetching and recording it when the store has none.</summary>
    /// <exception cref="FontFileException">The fetched bytes differ from the recorded hash, or exceed the size cap.</exception>
    public async Task<(string Sha256, byte[] Bytes)> GetOrFetchAsync(
        string commit, string path, Func<CancellationToken, Task<byte[]>> fetch, CancellationToken ct)
    {
        var key = $"{commit}/{path}";
        await gate.WaitAsync(ct);
        try
        {
            var known = await LoadIndexAsync(ct);
            if (known.TryGetValue(key, out var recorded))
            {
                var file = FilePath(recorded);
                if (File.Exists(file))
                {
                    return (recorded, await File.ReadAllBytesAsync(file, ct));
                }
            }

            var bytes = await fetch(ct);
            if (bytes.Length > MaxFileBytes)
            {
                throw new FontFileException($"{path} is larger than {MaxFileBytes} bytes.");
            }
            var hash = Convert.ToHexStringLower(SHA256.HashData(bytes));
            if (recorded is not null && recorded != hash)
            {
                throw new FontFileException($"{path} at {commit} no longer matches the hash recorded for it.");
            }

            Directory.CreateDirectory(root);
            var target = FilePath(hash);
            if (!File.Exists(target))
            {
                var temp = target + $".{Guid.NewGuid():N}.tmp";
                await File.WriteAllBytesAsync(temp, bytes, ct);
                File.Move(temp, target, overwrite: true);
            }
            if (recorded is null)
            {
                known[key] = hash;
                var tempIndex = indexPath + $".{Guid.NewGuid():N}.tmp";
                await File.WriteAllTextAsync(tempIndex, JsonSerializer.Serialize(known), ct);
                File.Move(tempIndex, indexPath, overwrite: true);
            }
            return (hash, bytes);
        }
        finally
        {
            gate.Release();
        }
    }

    private string FilePath(string sha256) => Path.Combine(root, sha256 + ".ttf");

    private async Task<Dictionary<string, string>> LoadIndexAsync(CancellationToken ct)
    {
        if (index is not null)
        {
            return index;
        }
        index = File.Exists(indexPath)
            ? JsonSerializer.Deserialize<Dictionary<string, string>>(await File.ReadAllTextAsync(indexPath, ct)) ?? []
            : [];
        return index;
    }
}
