using System.Globalization;
using System.Text.Json;
using Canvas.Server.Storage;
using Microsoft.Data.Sqlite;

namespace Canvas.Server.Documents;

public enum OpsQueryStatus { Ok, NotFound, Compacted }

public sealed record OpsQueryResult(OpsQueryStatus Status, OpsPage? Page = null, long SnapshotSeq = 0);

public enum SnapshotSaveStatus { Saved, NotFound, NotNewer, BeyondHead }

public enum AppendStatus { Appended, NotFound, Conflict }

/// <summary>Where a document's log stands.</summary>
public sealed record DocumentHead(long HeadSeq, long SnapshotSeq);

public sealed class DocumentStore(Database database, TimeProvider clock)
{
    public const int MaxOpsPerPage = 5000;

    private const int SqliteConstraint = 19;

    public async Task<IReadOnlyList<DocumentSummary>> ListAsync(CancellationToken ct)
    {
        await using var connection = await database.OpenAsync(ct);
        await using var command = connection.CreateCommand();
        command.CommandText = """
            SELECT id, name, created_at, updated_at, head_seq
            FROM documents
            ORDER BY updated_at DESC
            """;

        var documents = new List<DocumentSummary>();
        await using var reader = await command.ExecuteReaderAsync(ct);
        while (await reader.ReadAsync(ct))
        {
            documents.Add(ReadSummary(reader));
        }
        return documents;
    }

    public async Task<DocumentSummary> CreateAsync(string name, CancellationToken ct, string? snapshot = null)
    {
        var now = clock.GetUtcNow();
        var document = new DocumentSummary(Guid.CreateVersion7(now), name.Trim(), now, now, snapshot is null ? 0 : 1);

        await using var connection = await database.OpenAsync(ct);
        await using var command = connection.CreateCommand();
        command.CommandText = """
            INSERT INTO documents (id, name, created_at, updated_at, head_seq, snapshot_seq, snapshot)
            VALUES ($id, $name, $now, $now, $seq, $seq, $snapshot)
            """;
        command.Parameters.AddWithValue("$id", Key(document.Id));
        command.Parameters.AddWithValue("$name", document.Name);
        command.Parameters.AddWithValue("$now", Timestamp(now));
        command.Parameters.AddWithValue("$seq", document.HeadSeq);
        command.Parameters.AddWithValue("$snapshot", (object?)snapshot ?? DBNull.Value);
        await command.ExecuteNonQueryAsync(ct);

        return document;
    }

    public async Task<DocumentDetail?> GetAsync(Guid id, CancellationToken ct)
    {
        await using var connection = await database.OpenAsync(ct);
        await using var command = connection.CreateCommand();
        command.CommandText = """
            SELECT id, name, created_at, updated_at, head_seq, snapshot_seq, snapshot
            FROM documents
            WHERE id = $id
            """;
        command.Parameters.AddWithValue("$id", Key(id));

        await using var reader = await command.ExecuteReaderAsync(ct);
        if (!await reader.ReadAsync(ct))
        {
            return null;
        }

        var summary = ReadSummary(reader);
        var snapshot = reader.IsDBNull(6)
            ? null
            : new DocumentSnapshot(reader.GetInt64(5), ParseJson(reader.GetString(6)));

        return new DocumentDetail(summary.Id, summary.Name, summary.CreatedAt, summary.UpdatedAt, summary.HeadSeq, snapshot);
    }

    public async Task<DocumentSummary?> RenameAsync(Guid id, string name, CancellationToken ct)
    {
        await using var connection = await database.OpenAsync(ct);
        await using var command = connection.CreateCommand();
        command.CommandText = """
            UPDATE documents SET name = $name, updated_at = $now
            WHERE id = $id
            RETURNING id, name, created_at, updated_at, head_seq
            """;
        command.Parameters.AddWithValue("$id", Key(id));
        command.Parameters.AddWithValue("$name", name.Trim());
        command.Parameters.AddWithValue("$now", Timestamp(clock.GetUtcNow()));

        await using var reader = await command.ExecuteReaderAsync(ct);
        return await reader.ReadAsync(ct) ? ReadSummary(reader) : null;
    }

    public async Task<bool> DeleteAsync(Guid id, CancellationToken ct)
    {
        await using var connection = await database.OpenAsync(ct);
        await using var command = connection.CreateCommand();
        command.CommandText = "DELETE FROM documents WHERE id = $id";
        command.Parameters.AddWithValue("$id", Key(id));
        return await command.ExecuteNonQueryAsync(ct) > 0;
    }

    public async Task<DocumentHead?> GetHeadAsync(Guid id, CancellationToken ct)
    {
        await using var connection = await database.OpenAsync(ct);
        await using var command = connection.CreateCommand();
        command.CommandText = "SELECT head_seq, snapshot_seq FROM documents WHERE id = $id";
        command.Parameters.AddWithValue("$id", Key(id));
        await using var reader = await command.ExecuteReaderAsync(ct);
        return await reader.ReadAsync(ct) ? new DocumentHead(reader.GetInt64(0), reader.GetInt64(1)) : null;
    }

    /// <summary>The sequence numbers of the ops among <paramref name="opIds"/> that are already in the log.</summary>
    public async Task<IReadOnlyDictionary<Guid, long>> FindOpsAsync(Guid documentId, IReadOnlyCollection<Guid> opIds, CancellationToken ct)
    {
        var found = new Dictionary<Guid, long>();
        if (opIds.Count == 0)
        {
            return found;
        }

        await using var connection = await database.OpenAsync(ct);
        await using var command = connection.CreateCommand();
        var names = opIds.Select((_, i) => $"$op{i}").ToList();
        command.CommandText = $"SELECT op_id, seq FROM ops WHERE document_id = $document AND op_id IN ({string.Join(", ", names)})";
        command.Parameters.AddWithValue("$document", Key(documentId));
        foreach (var (name, id) in names.Zip(opIds))
        {
            command.Parameters.AddWithValue(name, Key(id));
        }

        await using var reader = await command.ExecuteReaderAsync(ct);
        while (await reader.ReadAsync(ct))
        {
            found[Guid.Parse(reader.GetString(0))] = reader.GetInt64(1);
        }
        return found;
    }

    /// <summary>
    /// Appends <paramref name="ops"/> after <paramref name="expectedHead"/>, numbering them from there.
    /// Nothing is written if another writer moved the head first or already logged one of the op ids.
    /// </summary>
    public async Task<AppendStatus> AppendAsync(Guid documentId, long expectedHead, IReadOnlyList<OpInput> ops, CancellationToken ct)
    {
        await using var connection = await database.OpenAsync(ct);
        await using var transaction = connection.BeginTransaction(deferred: false);

        var head = await ScalarAsync<long?>(connection, "SELECT head_seq FROM documents WHERE id = $id",
            ("$id", Key(documentId)));
        if (head is null)
        {
            return AppendStatus.NotFound;
        }
        if (head != expectedHead)
        {
            return AppendStatus.Conflict;
        }
        if (ops.Count == 0)
        {
            return AppendStatus.Appended;
        }

        var now = Timestamp(clock.GetUtcNow());
        try
        {
            foreach (var op in ops)
            {
                head++;
                await ExecuteAsync(connection, """
                    INSERT INTO ops (document_id, seq, op_id, kind, data, created_at)
                    VALUES ($document, $seq, $op, $kind, $data, $now)
                    """,
                    ("$document", Key(documentId)), ("$seq", head), ("$op", Key(op.Id)),
                    ("$kind", op.Kind), ("$data", op.Data.GetRawText()), ("$now", now));
            }
        }
        catch (SqliteException e) when (e.SqliteErrorCode == SqliteConstraint)
        {
            return AppendStatus.Conflict;
        }

        await ExecuteAsync(connection, "UPDATE documents SET head_seq = $head, updated_at = $now WHERE id = $id",
            ("$head", head), ("$now", now), ("$id", Key(documentId)));
        await transaction.CommitAsync(ct);
        return AppendStatus.Appended;
    }

    public async Task<OpsQueryResult> GetOpsAsync(Guid documentId, long after, int limit, CancellationToken ct)
    {
        await using var connection = await database.OpenAsync(ct);
        await using var transaction = connection.BeginTransaction(deferred: true);

        await using var header = connection.CreateCommand();
        header.CommandText = "SELECT head_seq, snapshot_seq FROM documents WHERE id = $id";
        header.Parameters.AddWithValue("$id", Key(documentId));
        long head, snapshotSeq;
        await using (var reader = await header.ExecuteReaderAsync(ct))
        {
            if (!await reader.ReadAsync(ct))
            {
                return new OpsQueryResult(OpsQueryStatus.NotFound);
            }
            head = reader.GetInt64(0);
            snapshotSeq = reader.GetInt64(1);
        }

        if (after < snapshotSeq)
        {
            return new OpsQueryResult(OpsQueryStatus.Compacted, SnapshotSeq: snapshotSeq);
        }

        await using var command = connection.CreateCommand();
        command.CommandText = """
            SELECT seq, op_id, kind, data, created_at
            FROM ops
            WHERE document_id = $document AND seq > $after
            ORDER BY seq
            LIMIT $limit
            """;
        command.Parameters.AddWithValue("$document", Key(documentId));
        command.Parameters.AddWithValue("$after", after);
        command.Parameters.AddWithValue("$limit", Math.Clamp(limit, 1, MaxOpsPerPage));

        var ops = new List<OpRecord>();
        await using (var reader = await command.ExecuteReaderAsync(ct))
        {
            while (await reader.ReadAsync(ct))
            {
                ops.Add(new OpRecord(
                    reader.GetInt64(0),
                    Guid.Parse(reader.GetString(1)),
                    reader.GetString(2),
                    ParseJson(reader.GetString(3)),
                    ParseTimestamp(reader.GetString(4))));
            }
        }

        return new OpsQueryResult(OpsQueryStatus.Ok, new OpsPage(ops, head));
    }

    /// <summary>
    /// Stores the snapshot and drops the payloads of the ops it folds in. Op ids are kept so a
    /// client retrying an already-compacted op still gets its original sequence number.
    /// </summary>
    public async Task<SnapshotSaveStatus> SaveSnapshotAsync(Guid documentId, long seq, string data, CancellationToken ct)
    {
        await using var connection = await database.OpenAsync(ct);
        await using var transaction = connection.BeginTransaction(deferred: false);

        await using var header = connection.CreateCommand();
        header.CommandText = "SELECT head_seq, snapshot_seq FROM documents WHERE id = $id";
        header.Parameters.AddWithValue("$id", Key(documentId));
        long head, snapshotSeq;
        await using (var reader = await header.ExecuteReaderAsync(ct))
        {
            if (!await reader.ReadAsync(ct))
            {
                return SnapshotSaveStatus.NotFound;
            }
            head = reader.GetInt64(0);
            snapshotSeq = reader.GetInt64(1);
        }

        if (seq <= snapshotSeq)
        {
            return SnapshotSaveStatus.NotNewer;
        }
        if (seq > head)
        {
            return SnapshotSaveStatus.BeyondHead;
        }

        await ExecuteAsync(connection, "UPDATE documents SET snapshot_seq = $seq, snapshot = $data WHERE id = $id",
            ("$seq", seq), ("$data", data), ("$id", Key(documentId)));
        await ExecuteAsync(connection, "UPDATE ops SET data = NULL WHERE document_id = $document AND seq <= $seq",
            ("$document", Key(documentId)), ("$seq", seq));

        await transaction.CommitAsync(ct);
        return SnapshotSaveStatus.Saved;
    }

    private static DocumentSummary ReadSummary(SqliteDataReader reader) => new(
        Guid.Parse(reader.GetString(0)),
        reader.GetString(1),
        ParseTimestamp(reader.GetString(2)),
        ParseTimestamp(reader.GetString(3)),
        reader.GetInt64(4));

    private static async Task<T?> ScalarAsync<T>(SqliteConnection connection, string sql, params (string Name, object? Value)[] parameters)
    {
        await using var command = connection.CreateCommand();
        command.CommandText = sql;
        foreach (var (name, value) in parameters)
        {
            command.Parameters.AddWithValue(name, value ?? DBNull.Value);
        }
        var result = await command.ExecuteScalarAsync();
        return result is null or DBNull ? default : (T)Convert.ChangeType(result, Nullable.GetUnderlyingType(typeof(T)) ?? typeof(T), CultureInfo.InvariantCulture);
    }

    private static async Task ExecuteAsync(SqliteConnection connection, string sql, params (string Name, object? Value)[] parameters)
    {
        await using var command = connection.CreateCommand();
        command.CommandText = sql;
        foreach (var (name, value) in parameters)
        {
            command.Parameters.AddWithValue(name, value ?? DBNull.Value);
        }
        await command.ExecuteNonQueryAsync();
    }

    private static string Key(Guid id) => id.ToString("D");

    private static string Timestamp(DateTimeOffset value) => value.ToString("O", CultureInfo.InvariantCulture);

    private static DateTimeOffset ParseTimestamp(string value) =>
        DateTimeOffset.Parse(value, CultureInfo.InvariantCulture, DateTimeStyles.RoundtripKind);

    private static JsonElement ParseJson(string json)
    {
        using var document = JsonDocument.Parse(json);
        return document.RootElement.Clone();
    }
}
