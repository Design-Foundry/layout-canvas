using System.Net.Http.Json;
using Microsoft.AspNetCore.Mvc.Testing;

namespace Canvas.Server.Tests;

public sealed class HealthEndpointTests(WebApplicationFactory<Program> factory)
    : IClassFixture<WebApplicationFactory<Program>>
{
    [Fact]
    public async Task Health_reports_ok()
    {
        var client = factory.CreateClient();

        var health = await client.GetFromJsonAsync<HealthResponse>("/api/health", TestContext.Current.CancellationToken);

        Assert.Equal("ok", health?.Status);
    }
}
