using System.Text.Json;
using Canvas.Server.Documents;
using Canvas.Server.VisualCore;

namespace Canvas.Server.Tests;

public sealed class VisualCoreDocumentTests
{
    private static readonly Guid FrameId = Guid.Parse("0192a4b1-7c3d-4e5f-8a6b-000000000100");

    private static VisualCoreOp CreateFrame(Guid id, Guid? parent = null) => new("CreateNode", JsonSerializer.SerializeToElement(new
    {
        node = new { id, type = "frame", order = "V", name = "Card", w = 200.0, h = 100.0, parent },
    }, new JsonSerializerOptions { DefaultIgnoreCondition = System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingNull }));

    [Fact]
    public void Valid_ops_apply_and_invalid_ones_are_reported()
    {
        using var document = VisualCoreDocument.Open(null);

        var refusals = document.Apply([CreateFrame(FrameId), CreateFrame(Guid.NewGuid(), parent: Guid.NewGuid())]);

        Assert.Null(refusals[0]);
        Assert.Equal(new OpRefusal("ops[1]", "the parent does not exist or cannot have children"), refusals[1]);
        var layers = document.Layers();
        Assert.Equal(1, layers.GetArrayLength());
        Assert.Equal("Card", layers[0].GetProperty("name").GetString());
    }

    [Fact]
    public void Nodes_report_their_laid_out_box()
    {
        using var document = VisualCoreDocument.Open(null);
        document.Apply([CreateFrame(FrameId)]);

        var box = document.Node(FrameId)!.Value.GetProperty("box");

        Assert.Equal(200, box.GetProperty("w").GetDouble());
        Assert.Null(document.Node(Guid.NewGuid()));
        Assert.Null(document.Tokens(Guid.NewGuid()));
    }

    [Fact]
    public void A_snapshot_reopens_as_the_same_document()
    {
        using var document = VisualCoreDocument.Open(null);
        document.Apply([CreateFrame(FrameId)]);
        var snapshot = document.Snapshot().GetRawText();

        using var reopened = VisualCoreDocument.Open(snapshot);

        Assert.Equal(0, reopened.RejectedOnLoad);
        Assert.Equal(snapshot, reopened.Snapshot().GetRawText());
    }

    [Fact]
    public void Malformed_input_is_refused()
    {
        using var document = VisualCoreDocument.Open(null);

        var rejected = Assert.Throws<SnapshotRejectedException>(() => VisualCoreDocument.Open("{}"));
        Assert.NotEmpty(rejected.Reason.Message);
        Assert.Throws<ArgumentException>(() => document.Apply([new VisualCoreOp("Unknown", JsonSerializer.SerializeToElement(new { }))]));
    }

    [Fact]
    public void A_snapshot_with_refused_layers_explains_the_first()
    {
        var snapshot = $$"""
            {"version":2,"modes":[],"tokens":[],"nodes":[
              {"id":"{{FrameId}}","type":"frame","order":"V","w":200,"h":100},
              {"id":"{{Guid.NewGuid()}}","type":"rect","order":"W","parent":"{{Guid.NewGuid()}}"}]}
            """;

        using var document = VisualCoreDocument.Open(snapshot);

        Assert.Equal(1, document.RejectedOnLoad);
        Assert.Equal("the parent does not exist or cannot have children", document.FirstRejection?.Message);
    }
}
