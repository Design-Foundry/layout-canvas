using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.RegularExpressions;
using Canvas.Server.Documents;

namespace Canvas.Server.Mcp;

/// <summary>An op as an agent writes it: <c>$name</c> placeholders allowed wherever an id goes.</summary>
public sealed record AgentOp(string Kind, JsonElement Data);

/// <summary>
/// Turns agent ops into log ops: every <c>$name</c> becomes the same new UUID throughout the batch, and a
/// <c>CreateNode</c> without <c>order</c> goes after its siblings, and an unnamed instance takes its component's name.
/// </summary>
public static partial class OpBatch
{
    private const string Digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

    [GeneratedRegex(@"^\$[A-Za-z][A-Za-z0-9_-]*$")]
    private static partial Regex Placeholder();

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
            if (op.Kind == "CreateNode" && data?["node"] is JsonObject node)
            {
                var parent = Guid.TryParse(Text(node["parent"]), out var p) ? p : Guid.Empty;
                var type = Text(node["type"]);
                if (type == "component" && Guid.TryParse(Text(node["id"]), out var componentId))
                {
                    names[componentId] = Text(node["name"]) ?? "";
                }
                if (type == "instance" && string.IsNullOrEmpty(Text(node["name"])) &&
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
                    var name = Placeholder().IsMatch(key) ? IdFor(key, ids).ToString() : key;
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
            case JsonValue value when value.TryGetValue<string>(out var text) && Placeholder().IsMatch(text):
                return JsonValue.Create(IdFor(text, ids).ToString());
            default:
                return node;
        }
    }

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
