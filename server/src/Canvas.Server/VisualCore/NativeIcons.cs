using System.Text.Json;
using Canvas.Server.Documents;

namespace Canvas.Server.VisualCore;

/// <summary>Registers icon SVGs with visualcore, which keeps them for the whole process.</summary>
public static class NativeIcons
{
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);

    /// <summary>The registry key of an icon reference given as a path string or <c>{ set, glyph, axes }</c> JSON, or null when it is not one.</summary>
    public static string? SourceKey(string iconJson) => NativeMethods.Take(NativeMethods.IconSourceKey(iconJson));

    /// <summary>The structured <c>{ set, glyph, axes }</c> JSON of an icon reference given in either form, or null when it is not one.</summary>
    public static string? Normalize(string iconJson) => NativeMethods.Take(NativeMethods.IconNormalize(iconJson));

    /// <summary>Registers the SVG of the icon <paramref name="key"/>. Returns the refusal, or null when registered.</summary>
    public static OpRefusal? Register(string key, string svg) =>
        NativeMethods.RegisterIcon(key, svg) == 1
            ? null
            : NativeMethods.Take(NativeMethods.LastError()) is { } json
                ? JsonSerializer.Deserialize<OpRefusal>(json, JsonOptions)
                : new OpRefusal("icon", "visualcore refused the icon");
}
