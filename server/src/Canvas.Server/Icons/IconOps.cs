using System.Text.Json;
using System.Text.Json.Nodes;
using Canvas.Server.VisualCore;

namespace Canvas.Server.Icons;

/// <summary>Writes icons in the form documents store, whichever form an op arrived in.</summary>
public static class IconOps
{
    /// <summary>
    /// <paramref name="data"/> with every <c>icon</c> given as a path string, such as
    /// <c>material-symbols/arrow_back?style=rounded</c>, replaced by its <c>{ set, glyph, axes }</c> object. A value that is
    /// not a valid icon stays as it is, so visualcore can refuse the op with the reason.
    /// </summary>
    public static JsonElement Normalize(JsonElement data)
    {
        if (data.ValueKind != JsonValueKind.Object || !data.GetRawText().Contains("\"icon\"", StringComparison.Ordinal))
        {
            return data;
        }
        var node = JsonNode.Parse(data.GetRawText());
        return Normalize(node) ? JsonSerializer.SerializeToElement(node) : data;
    }

    private static bool Normalize(JsonNode? node)
    {
        var changed = false;
        switch (node)
        {
            case JsonObject obj:
                foreach (var (key, value) in obj.ToList())
                {
                    if (key is "componentValues" or "when" or "values")
                    {
                        continue;
                    }
                    if (key == "icon" && value is JsonValue text && text.TryGetValue<string>(out var path))
                    {
                        if (NativeIcons.Normalize(JsonSerializer.Serialize(path)) is { } structured)
                        {
                            obj[key] = JsonNode.Parse(structured);
                            changed = true;
                        }
                    }
                    else
                    {
                        changed |= Normalize(value);
                    }
                }
                break;
            case JsonArray array:
                foreach (var item in array)
                {
                    changed |= Normalize(item);
                }
                break;
        }
        return changed;
    }
}
