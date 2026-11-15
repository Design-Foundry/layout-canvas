using System.Text.Json;
using Canvas.Server.Documents;

namespace Canvas.Server.VisualCore;

/// <summary>Registers font families and faces with visualcore, which keeps them for the whole process.</summary>
public static class NativeFonts
{
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);

    /// <summary>Registers a face's bytes under <paramref name="id"/>. Returns the refusal, or null when registered.</summary>
    public static OpRefusal? RegisterFace(string id, ReadOnlySpan<byte> bytes, string sha256Hex) =>
        NativeMethods.RegisterFontFace(id, bytes, (nuint)bytes.Length, sha256Hex) == 1
            ? null
            : LastError() ?? new OpRefusal("font", "visualcore refused the font face");

    /// <summary>Declares a family's files, <paramref name="filesJson"/> being <c>[{ "face", "fixed": { tag: value } }]</c>.</summary>
    public static OpRefusal? RegisterFamily(string provider, string key, string version, string filesJson) =>
        NativeMethods.RegisterFontFamily(provider, key, version, filesJson) == 1
            ? null
            : LastError() ?? new OpRefusal("font", "visualcore refused the font family");

    /// <summary>The axes and features the bytes offer, or null when they are not a usable OpenType face.</summary>
    public static JsonElement? Capabilities(ReadOnlySpan<byte> bytes)
    {
        if (NativeMethods.Take(NativeMethods.FontCapabilities(bytes, (nuint)bytes.Length)) is not { } json)
        {
            return null;
        }
        using var document = JsonDocument.Parse(json);
        return document.RootElement.Clone();
    }

    private static OpRefusal? LastError() =>
        NativeMethods.Take(NativeMethods.LastError()) is { } json ? JsonSerializer.Deserialize<OpRefusal>(json, JsonOptions) : null;
}
