using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.RegularExpressions;
using Canvas.Server.Documents;

namespace Canvas.Server.Mcp;

/// <summary>An op as an agent writes it: <c>$name</c> placeholders allowed wherever an id goes.</summary>
public sealed record AgentOp(string Kind, JsonElement Data);

/// <summary>
/// Turns agent ops into log ops: every <c>$name</c> becomes the same new UUID throughout the batch, and a
/// <c>CreateNode</c> or <c>CloneNode</c> without <c>order</c> goes after its siblings, and an unnamed instance takes its
/// component's name. <c>$root/$source</c> is the id of the copy of <c>$source</c> made by the <c>CloneNode</c> whose new id
/// is <c>$root</c>; either side may also be a UUID.
/// </summary>
public static partial class OpBatch
{
    private const string Digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

    [GeneratedRegex(@"^\$[A-Za-z][A-Za-z0-9_-]*$")]
    private static partial Regex Placeholder();

    [GeneratedRegex(@"^(?<scope>\$[A-Za-z][A-Za-z0-9_-]*|[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})/(?<node>\$[A-Za-z][A-Za-z0-9_-]*|[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})$")]
    private static partial Regex CopyReference();

    /// <param name="lastOrders">The greatest order among the existing children of each parent, the root under <see cref="Guid.Empty"/>.</param>
    /// <param name="componentNames">The names of the document's main components.</param>
    public static (List<OpInput> Ops, Dictionary<string, Guid> Ids) Prepare(
        IReadOnlyList<AgentOp> ops, IReadOnlyDictionary<Guid, string> lastOrders, IReadOnlyDictionary<Guid, string> componentNames)
    {
        var ids = new Dictionary<string, Guid>();
        var last = new Dictionary<Guid, string>(lastOrders);
        var names = new Dictionary<Guid, string>(componentNames);
        var prepared = new List<OpInput>(ops.Count);
        foreach (var op in ops)
        {
            var data = Resolve(JsonNode.Parse(op.Data.GetRawText()), ids);
            var node = op.Kind switch { "CreateNode" => data?["node"], "CloneNode" => data, _ => null } as JsonObject;
            if (node is not null)
            {
                var parent = Guid.TryParse(Text(node["parent"]), out var p) ? p : Guid.Empty;
                var type = Text(node["type"]);
                if (op.Kind == "CreateNode" && type == "component" && Guid.TryParse(Text(node["id"]), out var componentId))
                {
                    names[componentId] = Text(node["name"]) ?? "";
                }
                if (op.Kind == "CreateNode" && type == "instance" && string.IsNullOrEmpty(Text(node["name"])) &&
                    Guid.TryParse(Text(node["component"]), out var main) && names.TryGetValue(main, out var mainName))
                {
                    node["name"] = mainName;
                }
                if (node["order"] is null)
                {
                    node["order"] = After(last.GetValueOrDefault(parent));
                }
                if (node["order"] is JsonValue value && value.TryGetValue<string>(out var order) &&
                    string.CompareOrdinal(order, last.GetValueOrDefault(parent)) > 0)
                {
                    last[parent] = order;
                }
            }
            var element = data is null ? default : JsonSerializer.SerializeToElement(data);
            prepared.Add(new OpInput(Guid.CreateVersion7(), op.Kind, element));
        }
        return (prepared, ids);
    }

    /// <summary>A fractional index that sorts after <paramref name="last"/>, short while siblings stay few.</summary>
    public static string After(string? last)
    {
        if (string.IsNullOrEmpty(last))
        {
            return "a0";
        }
        if (last.Length == 2 && last[0] == 'a' && Digits.IndexOf(last[1]) is var digit and >= 0 and < 61)
        {
            return $"a{Digits[digit + 1]}";
        }
        return last + "V";
    }

    private static string? Text(JsonNode? node) => node is JsonValue value && value.TryGetValue<string>(out var text) ? text : null;

    private static JsonNode? Resolve(JsonNode? node, Dictionary<string, Guid> ids)
    {
        switch (node)
        {
            case JsonObject obj:
                foreach (var (key, value) in obj.ToList())
                {
                    var resolved = Resolve(value, ids);
                    var name = Reference(key, ids) ?? key;
                    obj.Remove(key);
                    obj[name] = resolved;
                }
                return obj;
            case JsonArray array:
                for (var i = 0; i < array.Count; i++)
                {
                    var item = array[i];
                    array[i] = null;
                    array[i] = Resolve(item, ids);
                }
                return array;
            case JsonValue value when value.TryGetValue<string>(out var text) && Reference(text, ids) is { } reference:
                return JsonValue.Create(reference);
            default:
                return node;
        }
    }

    private static string? Reference(string text, Dictionary<string, Guid> ids)
    {
        if (Placeholder().IsMatch(text))
        {
            return IdFor(text, ids).ToString();
        }
        var copy = CopyReference().Match(text);
        return copy.Success ? CloneIds.Derive(Part(copy.Groups["scope"].Value, ids), Part(copy.Groups["node"].Value, ids)).ToString() : null;
    }

    private static Guid Part(string text, Dictionary<string, Guid> ids) => text[0] == '$' ? IdFor(text, ids) : Guid.Parse(text);

    private static Guid IdFor(string placeholder, Dictionary<string, Guid> ids)
    {
        if (!ids.TryGetValue(placeholder, out var id))
        {
            id = Guid.CreateVersion7();
            ids[placeholder] = id;
        }
        return id;
    }
}
