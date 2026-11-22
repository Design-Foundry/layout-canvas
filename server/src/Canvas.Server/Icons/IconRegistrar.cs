using System.Collections.Concurrent;
using System.Text.Json;
using System.Text.RegularExpressions;
using Canvas.Server.VisualCore;

namespace Canvas.Server.Icons;

/// <summary>
/// Makes sure visualcore knows the SVG of every icon a snapshot or a batch of ops names before it applies them.
/// Registration is per process; a glyph the source lacks is settled and draws as a missing glyph, and a failure to
/// reach GitHub is logged and retried after a minute.
/// </summary>
public sealed partial class IconRegistrar(MaterialSymbolsProvider provider, TimeProvider clock, ILogger<IconRegistrar> logger)
{
    private static readonly TimeSpan RetryAfter = TimeSpan.FromMinutes(1);

    private readonly ConcurrentDictionary<string, long?> outcomes = new();
    private readonly SemaphoreSlim gate = new(1, 1);

    [GeneratedRegex(@"^material-symbols/(?<glyph>[a-z0-9_]{1,64})\?style=(?<style>[a-z]+)&FILL=(?<fill>[01])&wght=(?<weight>\d+)&opsz=(?<size>\d+)$")]
    private static partial Regex SourceKeyPattern();

    /// <summary>Registers the icons used by the nodes, overrides and rules of <paramref name="snapshot"/>. Never throws for icon problems.</summary>
    public Task EnsureSnapshotAsync(string? snapshot, CancellationToken ct)
    {
        if (snapshot is null || !snapshot.Contains("\"icon\"", StringComparison.Ordinal))
        {
            return Task.CompletedTask;
        }
        try
        {
            using var document = JsonDocument.Parse(snapshot);
            return EnsureAsync(Collect(document.RootElement), ct);
        }
        catch (JsonException)
        {
            return Task.CompletedTask;
        }
    }

    /// <summary>Registers the icons that <paramref name="ops"/> name in nodes and patches. Never throws for icon problems.</summary>
    public Task EnsureOpsAsync(IEnumerable<VisualCoreOp> ops, CancellationToken ct) =>
        EnsureAsync(ops.SelectMany(op => Collect(op.Data)).Distinct().ToList(), ct);

    private static List<string> Collect(JsonElement element)
    {
        var seen = new HashSet<string>();
        var keys = new List<string>();
        Walk(element, seen, keys);
        return keys;
    }

    private static void Walk(JsonElement element, HashSet<string> seen, List<string> keys)
    {
        switch (element.ValueKind)
        {
            case JsonValueKind.Object:
                foreach (var property in element.EnumerateObject())
                {
                    if (property.Name == "icon" && property.Value.ValueKind is JsonValueKind.String or JsonValueKind.Object)
                    {
                        var text = property.Value.GetRawText();
                        if (seen.Add(text) && NativeIcons.SourceKey(text) is { } key)
                        {
                            keys.Add(key);
                        }
                    }
                    else
                    {
                        Walk(property.Value, seen, keys);
                    }
                }
                break;
            case JsonValueKind.Array:
                foreach (var item in element.EnumerateArray())
                {
                    Walk(item, seen, keys);
                }
                break;
        }
    }

    private async Task EnsureAsync(IReadOnlyList<string> keys, CancellationToken ct)
    {
        var unavailable = false;
        foreach (var key in keys.Distinct())
        {
            if (IsSettled(key))
            {
                continue;
            }
            if (unavailable)
            {
                outcomes[key] = clock.GetTimestamp();
                continue;
            }
            await gate.WaitAsync(ct);
            try
            {
                if (!IsSettled(key))
                {
                    outcomes[key] = await RegisterAsync(key, ct) ? null : clock.GetTimestamp();
                }
            }
            catch (OperationCanceledException) when (ct.IsCancellationRequested)
            {
                throw;
            }
            catch (Exception e)
            {
                logger.LogWarning(e, "Icon {Key} could not be registered", key);
                outcomes[key] = clock.GetTimestamp();
                unavailable = e is IconsUnavailableException;
            }
            finally
            {
                gate.Release();
            }
        }
    }

    private bool IsSettled(string key) =>
        outcomes.TryGetValue(key, out var failedAt) &&
        (failedAt is null || clock.GetElapsedTime(failedAt.Value) < RetryAfter);

    /// <returns>False when registration should be tried again later.</returns>
    private async Task<bool> RegisterAsync(string key, CancellationToken ct)
    {
        if (SourceKeyPattern().Match(key) is not { Success: true } match)
        {
            return true;
        }
        var variant = new IconVariant(match.Groups["style"].Value, int.Parse(match.Groups["fill"].Value),
            int.Parse(match.Groups["weight"].Value), int.Parse(match.Groups["size"].Value));
        var svg = await provider.GetSvgAsync(match.Groups["glyph"].Value, variant, provider.DefaultCommit, ct);
        if (svg is null)
        {
            logger.LogInformation("Icon {Key} does not exist at {Commit}", key, provider.DefaultCommit);
            return true;
        }
        if (NativeIcons.Register(key, svg) is { } refusal)
        {
            logger.LogWarning("visualcore refused the icon {Key}: {Message}", key, refusal.Message);
            return false;
        }
        return true;
    }
}
