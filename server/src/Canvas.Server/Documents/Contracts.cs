using System.ComponentModel.DataAnnotations;
using System.Text.Json;

namespace Canvas.Server.Documents;

/// <summary>A document as shown in the document list.</summary>
/// <param name="HeadSeq">Sequence number of the latest op in the log.</param>
public sealed record DocumentSummary(Guid Id, string Name, DateTimeOffset CreatedAt, DateTimeOffset UpdatedAt, long HeadSeq);

/// <summary>A compacted document state. Ops up to and including <paramref name="Seq"/> are folded into it.</summary>
/// <param name="Data">Opaque snapshot produced by visualcore.</param>
public sealed record DocumentSnapshot(long Seq, JsonElement Data);

/// <summary>Everything needed to open a document: its latest snapshot, then the ops after it.</summary>
public sealed record DocumentDetail(
    Guid Id,
    string Name,
    DateTimeOffset CreatedAt,
    DateTimeOffset UpdatedAt,
    long HeadSeq,
    DocumentSnapshot? Snapshot);

public sealed record CreateDocumentRequest([Required, StringLength(200)] string Name);

public sealed record RenameDocumentRequest([Required, StringLength(200)] string Name);

/// <summary>An edit produced by the client.</summary>
/// <param name="Id">Client-generated id. Resending an op with a known id returns its original sequence number.</param>
/// <param name="Kind">Op type, such as <c>CreateNode</c> or <c>SetProps</c>.</param>
/// <param name="Data">Op payload in visualcore's wire format.</param>
public sealed record OpInput(Guid Id, [Required, StringLength(64)] string Kind, JsonElement Data);

public sealed record AppendOpsRequest([Required, MinLength(1), MaxLength(AppendOpsRequest.MaxOps)] IReadOnlyList<OpInput> Ops)
{
    public const int MaxOps = 1000;
}

/// <summary>What the server did with an op.</summary>
/// <param name="Seq">Its sequence number in the log, or null if the document refused it and it was not logged.</param>
public sealed record OpAck(Guid Id, long? Seq);

public sealed record AppendOpsResponse(IReadOnlyList<OpAck> Acks, long HeadSeq);

/// <summary>An op as stored in the log.</summary>
public sealed record OpRecord(long Seq, Guid Id, string Kind, JsonElement Data, DateTimeOffset CreatedAt);

/// <summary>A page of the op log. Keep reading from the last <c>seq</c> until it reaches <paramref name="HeadSeq"/>.</summary>
public sealed record OpsPage(IReadOnlyList<OpRecord> Ops, long HeadSeq);
