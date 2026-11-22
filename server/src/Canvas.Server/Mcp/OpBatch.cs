using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.RegularExpressions;
using Canvas.Server.Documents;
using Canvas.Server.VisualCore;

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
    /// <param name="parents">The parent of each existing node, the root under <see cref="Guid.Empty"/>; it bounds which nodes <c>$root/$source</c> may name.</param>
    /// <exception cref="MalformedOpsException">A copy reference names an unknown placeholder, a clone that is not earlier in the batch, or a node outside that clone's source subtree.</exception>
    public static (List<OpInput> Ops, Dictionary<string, Guid> Ids) Prepare(
        IReadOnlyList<AgentOp> ops, IReadOnlyDictionary<Guid, string> lastOrders, IReadOnlyDictionary<Guid, string> componentNames,
        IReadOnlyDictionary<Guid, Guid>? parents = null)
    {
        var tree = new Dictionary<Guid, Guid>(parents ?? new Dictionary<Guid, Guid>());
        var clones = new Dictionary<Guid, HashSet<Guid>>();
        var ids = new Dictionary<string, Guid>();
        var last = new Dictionary<Guid, string>(lastOrders);
        var names = new Dictionary<Guid, string>(componentNames);
        var prepared = new List<OpInput>(ops.Count);
        for (var index = 0; index < ops.Count; index++)
        {
            var op = ops[index];
            var data = Resolve(JsonNode.Parse(op.Data.GetRawText()), ids, new Context(clones, $"ops[{index}]"));
            Track(op.Kind, data, tree, clones);
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

    private sealed record Context(Dictionary<Guid, HashSet<Guid>> Clones, string Path);

    private static void Track(string kind, JsonNode? data, Dictionary<Guid, Guid> tree, Dictionary<Guid, HashSet<Guid>> clones)
    {
        Guid? Id(JsonNode? value) => Guid.TryParse(Text(value), out var id) ? (Guid?)id : null;
        var target = kind == "CreateNode" ? data?["node"] : data;
        var self = Id(target?["id"]);
        switch (kind)
        {
            case "CreateNode" when self is not null:
                tree[self.Value] = Id(target?["parent"]) ?? Guid.Empty;
                break;
            case "MoveNode" when self is not null:
                tree[self.Value] = Id(target?["parent"]) ?? Guid.Empty;
                break;
            case "DeleteNode" when self is not null:
                tree.Remove(self.Value);
                break;
            case "CloneNode" when self is not null && Id(target?["source"]) is not null:
                var source = Id(target?["source"])!.Value;
                var root = self.Value;
                var subtree = new HashSet<Guid> { source };
                var pending = new Queue<Guid>([source]);
                var below = tree.ToLookup(entry => entry.Value, entry => entry.Key);
                var copies = new Dictionary<Guid, Guid> { [source] = root };
                tree[root] = Id(target?["parent"]) ?? Guid.Empty;
                while (pending.TryDequeue(out var current))
                {
                    foreach (var child in below[current].Where(subtree.Add).ToList())
                    {
                        var copy = CloneIds.Derive(root, child);
                        copies[child] = copy;
                        tree[copy] = copies[current];
                        pending.Enqueue(child);
                    }
                }
                clones[root] = subtree;
                break;
        }
    }

    private static JsonNode? Resolve(JsonNode? node, Dictionary<string, Guid> ids, Context context)
    {
        switch (node)
        {
            case JsonObject obj:
                foreach (var (key, value) in obj.ToList())
                {
                    var resolved = Resolve(value, ids, context with { Path = $"{context.Path}.{key}" });
                    var name = Reference(key, ids, context with { Path = $"{context.Path}.{key}" }) ?? key;
                    obj.Remove(key);
                    obj[name] = resolved;
                }
                return obj;
            case JsonArray array:
                for (var i = 0; i < array.Count; i++)
                {
                    var item = array[i];
                    array[i] = null;
                    array[i] = Resolve(item, ids, context with { Path = $"{context.Path}[{i}]" });
                }
                return array;
            case JsonValue value when value.TryGetValue<string>(out var text) && Reference(text, ids, context) is { } reference:
                return JsonValue.Create(reference);
            default:
                return node;
        }
    }

    private static string? Reference(string text, Dictionary<string, Guid> ids, Context context)
    {
        if (Placeholder().IsMatch(text))
        {
            return IdFor(text, ids).ToString();
        }
        var copy = CopyReference().Match(text);
        if (!copy.Success)
        {
            return null;
        }
        var scope = Part(copy.Groups["scope"].Value, ids, context);
        var node = Part(copy.Groups["node"].Value, ids, context);
        if (!context.Clones.TryGetValue(scope, out var subtree))
        {
            throw Refusal(context, $"{copy.Groups["scope"].Value} is not the id of a CloneNode earlier in the batch");
        }
        if (!subtree.Contains(node))
        {
            throw Refusal(context, $"{copy.Groups["node"].Value} is not a node of the source subtree of the clone {copy.Groups["scope"].Value}");
        }
        return CloneIds.Derive(scope, node).ToString();
    }

    private static MalformedOpsException Refusal(Context context, string message) => new(new OpRefusal(context.Path, message));

    private static Guid Part(string text, Dictionary<string, Guid> ids, Context context) =>
        text[0] != '$' ? Guid.Parse(text)
        : ids.TryGetValue(text, out var id) ? id
        : throw Refusal(context, $"{text} is not defined earlier in the batch");

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
