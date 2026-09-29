using Microsoft.Data.Sqlite;
using Microsoft.Extensions.Options;

namespace Canvas.Server.Storage;

public sealed class Database
{
    private readonly string connectionString;

    public Database(IOptions<StorageOptions> options, IHostEnvironment environment)
    {
        var path = Path.GetFullPath(options.Value.Database, environment.ContentRootPath);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);

        connectionString = new SqliteConnectionStringBuilder
        {
            DataSource = path,
            ForeignKeys = true,
        }.ToString();
    }

    public async Task<SqliteConnection> OpenAsync(CancellationToken cancellationToken)
    {
        var connection = new SqliteConnection(connectionString);
        await connection.OpenAsync(cancellationToken);
        return connection;
    }

    public async Task InitializeAsync(CancellationToken cancellationToken)
    {
        await using var connection = await OpenAsync(cancellationToken);
        await using var command = connection.CreateCommand();
        command.CommandText = """
            PRAGMA journal_mode = WAL;

            CREATE TABLE IF NOT EXISTS documents (
                id           TEXT    PRIMARY KEY,
                name         TEXT    NOT NULL,
                created_at   TEXT    NOT NULL,
                updated_at   TEXT    NOT NULL,
                head_seq     INTEGER NOT NULL DEFAULT 0,
                snapshot_seq INTEGER NOT NULL DEFAULT 0,
                snapshot     TEXT    NULL
            );

            CREATE TABLE IF NOT EXISTS ops (
                document_id TEXT    NOT NULL REFERENCES documents (id) ON DELETE CASCADE,
                seq         INTEGER NOT NULL,
                op_id       TEXT    NOT NULL,
                kind        TEXT    NOT NULL,
                data        TEXT    NULL,
                created_at  TEXT    NOT NULL,
                PRIMARY KEY (document_id, seq),
                UNIQUE (document_id, op_id)
            );
            """;
        await command.ExecuteNonQueryAsync(cancellationToken);
    }
}

public sealed class DatabaseInitializer(Database database) : IHostedService
{
    public Task StartAsync(CancellationToken cancellationToken) => database.InitializeAsync(cancellationToken);

    public Task StopAsync(CancellationToken cancellationToken) => Task.CompletedTask;
}
