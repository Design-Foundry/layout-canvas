using System.Collections.Concurrent;
using System.Text.Json;
using Canvas.Server.VisualCore;

namespace Canvas.Server.Fonts;

/// <summary>
/// Makes sure visualcore knows the files of every Google font a snapshot or a batch of ops names before it
/// lays text out. Registration is per process; a failure is logged and retried after a minute, and text
/// falls back to other fonts meanwhile.
/// </summary>
public sealed class GoogleFontsRegistrar(GoogleFontsProvider provider, TimeProvider clock, ILogger<GoogleFontsRegistrar> logger)
{
    private static readonly TimeSpan RetryAfter = TimeSpan.FromMinutes(1);

    private readonly ConcurrentDictionary<string, long?> outcomes = new();
    private readonly SemaphoreSlim gate = new(1, 1);

    /// <summary>Registers the Google fonts listed in <paramref name="snapshot"/>. Never throws for font problems.</summary>
    public Task EnsureSnapshotAsync(string? snapshot, CancellationToken ct)
    {
        if (snapshot is null)
        {
            return Task.CompletedTask;
        }
        var wanted = new List<(string Key, string Commit)>();
        try
        {
            using var document = JsonDocument.Parse(snapshot);
            if (document.RootElement.ValueKind == JsonValueKind.Object &&
                document.RootElement.TryGetProperty("fonts", out var fonts) && fonts.ValueKind == JsonValueKind.Array)
            {
                wanted.AddRange(fonts.EnumerateArray().Select(Wanted).OfType<(string, string)>());
            }
        }
        catch (JsonException)
        {
        }
        return EnsureAsync(wanted, ct);
    }

    /// <summary>Registers the Google fonts that <c>CreateFont</c> and <c>ReplaceFont</c> ops in <paramref name="ops"/> name.</summary>
    public Task EnsureOpsAsync(IEnumerable<VisualCoreOp> ops, CancellationToken ct)
    {
        var wanted = ops
            .Where(op => op.Kind is "CreateFont" or "ReplaceFont" && op.Data.ValueKind == JsonValueKind.Object)
            .Select(op => op.Data.TryGetProperty("font", out var font) ? Wanted(font) : null)
            .OfType<(string, string)>()
            .ToList();
        return EnsureAsync(wanted, ct);
    }

    private static (string Key, string Commit)? Wanted(JsonElement font)
    {
        if (font.ValueKind != JsonValueKind.Object ||
            !font.TryGetProperty("provider", out var provider) || provider.ValueKind != JsonValueKind.String ||
            provider.GetString() != GoogleFontsProvider.ProviderName ||
            !font.TryGetProperty("key", out var key) || key.ValueKind != JsonValueKind.String ||
            !font.TryGetProperty("familyVersion", out var version) || version.ValueKind != JsonValueKind.String)
        {
            return null;
        }
        return GoogleFontsProvider.IsValidKey(key.GetString()) && GoogleFontsProvider.IsValidCommit(version.GetString())
            ? (key.GetString()!, version.GetString()!)
            : null;
    }

    private async Task EnsureAsync(IReadOnlyList<(string Key, string Commit)> wanted, CancellationToken ct)
    {
        foreach (var (key, commit) in wanted.Distinct())
        {
            var id = $"{commit}/{key}";
            if (IsSettled(id))
            {
                continue;
            }
            await gate.WaitAsync(ct);
            try
            {
                if (!IsSettled(id))
                {
                    outcomes[id] = await RegisterAsync(key, commit, ct) ? null : clock.GetTimestamp();
                }
            }
            catch (OperationCanceledException) when (ct.IsCancellationRequested)
            {
                throw;
            }
            catch (Exception e)
            {
                logger.LogWarning(e, "Google font {Key} at {Commit} could not be registered", key, commit);
                outcomes[id] = clock.GetTimestamp();
            }
            finally
            {
                gate.Release();
            }
        }
    }

    private bool IsSettled(string id) =>
        outcomes.TryGetValue(id, out var failedAt) &&
        (failedAt is null || clock.GetElapsedTime(failedAt.Value) < RetryAfter);

    private async Task<bool> RegisterAsync(string key, string commit, CancellationToken ct)
    {
        var manifest = await provider.GetManifestAsync(commit, key, ct);
        if (manifest is null)
        {
            logger.LogWarning("Google font {Key} does not exist at {Commit}", key, commit);
            return false;
        }

        foreach (var entry in manifest)
        {
            var file = await provider.GetFileAsync(commit, key, entry.File, ct);
            if (file is null)
            {
                return false;
            }
            if (NativeFonts.RegisterFace(entry.Face, file.Value.Bytes, entry.Sha256) is { } refusal)
            {
                logger.LogWarning("visualcore refused {Face}: {Message}", entry.Face, refusal.Message);
                return false;
            }
        }

        var files = JsonSerializer.Serialize(manifest.Select(e => new { face = e.Face, @fixed = e.Fixed }));
        if (NativeFonts.RegisterFamily(GoogleFontsProvider.ProviderName, key, commit, files) is { } refused)
        {
            logger.LogWarning("visualcore refused the family {Key}: {Message}", key, refused.Message);
            return false;
        }
        return true;
    }
}
