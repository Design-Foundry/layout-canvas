using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Mvc.Testing;

namespace Canvas.Server.Tests;

public sealed class CanvasFactory : WebApplicationFactory<Program>
{
    public const int OpsPerSnapshot = 5;

    private readonly string databasePath = Path.Combine(Path.GetTempPath(), $"canvas-tests-{Guid.NewGuid():N}.db");

    protected override void ConfigureWebHost(IWebHostBuilder builder) => builder
        .UseSetting("Storage:Database", databasePath)
        .UseSetting("Documents:OpsPerSnapshot", OpsPerSnapshot.ToString(System.Globalization.CultureInfo.InvariantCulture));

    public override async ValueTask DisposeAsync()
    {
        await base.DisposeAsync();
        Microsoft.Data.Sqlite.SqliteConnection.ClearAllPools();
        foreach (var suffix in new[] { "", "-wal", "-shm" })
        {
            File.Delete(databasePath + suffix);
        }
    }
}
