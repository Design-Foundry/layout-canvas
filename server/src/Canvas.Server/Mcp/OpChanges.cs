using System.Text.Json;
using System.Text.Json.Nodes;
using Canvas.Server.Documents;

namespace Canvas.Server.Mcp;

/// <summary>One logged op as <c>get_changes</c> reports it.</summary>
/// <param name="NodeIds">Nodes the op created, changed, moved or deleted.</param>
/// <param name="Entity">The token, font, font stack or mode a non-node op targets.</param>
/// <param name="Fields">Properties the op sets.</param>
/// <param name="Values">The values, only when they are small.</param>
public sealed record Change(long Seq, string Actor, string Kind, IReadOnlyList<Guid> NodeIds, ChangeEntity? Entity, IReadOnlyList<string>? Fields, JsonNode? Values);

public sealed record ChangeEntity(string Type, Guid Id);

public static class OpChanges
{
    public const int MaxValuesLength = 240;

    public static Change Describe(OpRecord op)
    {
        var nodes = new List<Guid>();
        var fields = new List<string>();
        var values = new JsonObject();
        ChangeEntity? entity = null;
        Collect(op.Kind, op.Data, nodes, fields, values, ref entity);
        var small = values.Count > 0 && values.ToJsonString().Length <= MaxValuesLength;
        return new Change(op.Seq, op.Actor, op.Kind, nodes.Distinct().ToList(), entity,
            fields.Count > 0 ? fields.Distinct().ToList() : null, small ? values : null);
    }

    private static void Collect(string kind, JsonElement data, List<Guid> nodes, List<string> fields, JsonObject values, ref ChangeEntity? entity)
    {
        if (data.ValueKind != JsonValueKind.Object)
        {
            return;
        }
        switch (kind)
        {
            case "CreateNode" when data.TryGetProperty("node", out var node) && node.ValueKind == JsonValueKind.Object:
                AddId(nodes, node, "id");
                fields.AddRange(node.EnumerateObject().Select(p => p.Name).Where(n => n != "id"));
                foreach (var key in new[] { "type", "name", "parent" })
                {
                    if (node.TryGetProperty(key, out var value))
                    {
                        values[key] = JsonNode.Parse(value.GetRawText());
                    }
                }
                break;
            case "SetProps" or "SetOverride":
                AddId(nodes, data, "id");
                AddId(nodes, data, "node");
                AddPatch(data, "patch", fields, values);
                break;
            case "MoveNode":
                AddId(nodes, data, "id");
                fields.AddRange(["parent", "order"]);
                foreach (var key in new[] { "parent", "order" })
                {
                    if (data.TryGetProperty(key, out var value))
                    {
                        values[key] = JsonNode.Parse(value.GetRawText());
                    }
                }
                break;
            case "DeleteNode" or "SetComponentDefinition":
                AddId(nodes, data, "id");
                if (kind == "SetComponentDefinition")
                {
                    fields.Add("definition");
                }
                break;
            case "CloneNode":
                AddId(nodes, data, "id");
                AddId(nodes, data, "source");
                if (data.TryGetProperty("patches", out var patches) && patches.ValueKind == JsonValueKind.Object)
                {
                    foreach (var patch in patches.EnumerateObject())
                    {
                        if (Guid.TryParse(patch.Name, out var target))
                        {
                            nodes.Add(target);
                        }
                        if (patch.Value.ValueKind == JsonValueKind.Object)
                        {
                            fields.AddRange(patch.Value.EnumerateObject().Select(p => p.Name));
                        }
                    }
                }
                break;
            case "Batch" when data.TryGetProperty("ops", out var ops) && ops.ValueKind == JsonValueKind.Array:
                foreach (var inner in ops.EnumerateArray())
                {
                    if (inner.ValueKind == JsonValueKind.Object && inner.TryGetProperty("kind", out var innerKind) &&
                        innerKind.GetString() is { } name && inner.TryGetProperty("data", out var innerData))
                    {
                        Collect(name, innerData, nodes, fields, new JsonObject(), ref entity);
                    }
                }
                break;
            default:
                entity = Entity(kind, data);
                break;
        }
    }

    private static ChangeEntity? Entity(string kind, JsonElement data)
    {
        foreach (var (prefix, type, property) in new[] { ("Token", "token", "token"), ("FontStack", "fontStack", "stack"), ("Font", "font", "font"), ("Mode", "mode", "mode") })
        {
            if (!kind.EndsWith(prefix, StringComparison.Ordinal))
            {
                continue;
            }
            var holder = data.TryGetProperty(property, out var inner) && inner.ValueKind == JsonValueKind.Object ? inner : data;
            return holder.TryGetProperty("id", out var id) && Guid.TryParse(id.GetString(), out var guid) ? new ChangeEntity(type, guid) : null;
        }
        return null;
    }

    private static void AddId(List<Guid> nodes, JsonElement data, string name)
    {
        if (data.TryGetProperty(name, out var id) && id.ValueKind == JsonValueKind.String && Guid.TryParse(id.GetString(), out var guid))
        {
            nodes.Add(guid);
        }
    }

    private static void AddPatch(JsonElement data, string name, List<string> fields, JsonObject values)
    {
        if (!data.TryGetProperty(name, out var patch) || patch.ValueKind != JsonValueKind.Object)
        {
            return;
        }
        foreach (var property in patch.EnumerateObject())
        {
            fields.Add(property.Name);
            values[property.Name] = JsonNode.Parse(property.Value.GetRawText());
        }
    }
}
