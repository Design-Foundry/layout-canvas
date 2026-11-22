using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Mvc.Testing;
using Microsoft.AspNetCore.TestHost;
using Microsoft.Extensions.DependencyInjection;

namespace Canvas.Server.Tests;

public sealed class CanvasFactory : WebApplicationFactory<Program>
{
    public const int OpsPerSnapshot = 5;

    private readonly string databasePath = Path.Combine(Path.GetTempPath(), $"canvas-tests-{Guid.NewGuid():N}.db");

    private readonly string fontsPath = Path.Combine(Path.GetTempPath(), $"canvas-tests-fonts-{Guid.NewGuid():N}");

    private readonly string iconsPath = Path.Combine(Path.GetTempPath(), $"canvas-tests-icons-{Guid.NewGuid():N}");

    /// <summary>Stands in for GitHub: no test reaches the network.</summary>
    public FakeGitHub GitHub { get; } = FakeGitHub.WithFixtures();

    /// <summary>Where the server keeps the font files it downloaded.</summary>
    public string FontsPath => fontsPath;

    /// <summary>Where the server keeps the icon files it downloaded.</summary>
    public string IconsPath => iconsPath;

    protected override void ConfigureWebHost(IWebHostBuilder builder) => builder
        .UseSetting("Storage:Database", databasePath)
        .UseSetting("Storage:Fonts", fontsPath)
        .UseSetting("Storage:Icons", iconsPath)
        .UseSetting("Documents:OpsPerSnapshot", OpsPerSnapshot.ToString(System.Globalization.CultureInfo.InvariantCulture))
        .ConfigureTestServices(services => services.AddSingleton<IHttpClientFactory>(new FakeHttpClientFactory(GitHub)));

    public override async ValueTask DisposeAsync()
    {
        await base.DisposeAsync();
        Microsoft.Data.Sqlite.SqliteConnection.ClearAllPools();
        foreach (var suffix in new[] { "", "-wal", "-shm" })
        {
            File.Delete(databasePath + suffix);
        }
        if (Directory.Exists(iconsPath))
        {
            Directory.Delete(iconsPath, recursive: true);
        }
        if (Directory.Exists(fontsPath))
        {
            Directory.Delete(fontsPath, recursive: true);
        }
    }
}
