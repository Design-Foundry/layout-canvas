using System.Net.Http.Json;

namespace Canvas.Server.Tests;

public sealed class HealthEndpointTests(CanvasFactory factory) : IClassFixture<CanvasFactory>
{
    [Fact]
    public async Task Health_reports_ok()
    {
        var client = factory.CreateClient();

        var health = await client.GetFromJsonAsync<HealthResponse>("/api/health", TestContext.Current.CancellationToken);

        Assert.Equal("ok", health?.Status);
    }
}
