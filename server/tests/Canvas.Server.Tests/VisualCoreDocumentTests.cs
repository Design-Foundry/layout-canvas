using System.Text.Json;
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

        var applied = document.Apply([CreateFrame(FrameId), CreateFrame(Guid.NewGuid(), parent: Guid.NewGuid())]);

        Assert.Equal([true, false], applied);
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

        Assert.Throws<InvalidDataException>(() => VisualCoreDocument.Open("{}"));
        Assert.Throws<ArgumentException>(() => document.Apply([new VisualCoreOp("Unknown", JsonSerializer.SerializeToElement(new { }))]));
    }
}
