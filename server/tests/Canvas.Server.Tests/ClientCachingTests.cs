using Microsoft.AspNetCore.Http;

namespace Canvas.Server.Tests;

public sealed class ClientCachingTests
{
    [Theory]
    [InlineData("main-BVDGDYH2.js")]
    [InlineData("chunk-DuzGPXOC.js")]
    [InlineData("styles-5INURTSO.css")]
    public void Hashed_bundles_are_cached_for_good(string fileName)
    {
        var response = new DefaultHttpContext().Response;

        ClientCaching.Apply(response, fileName);

        Assert.Equal(ClientCaching.Immutable, response.Headers.CacheControl.ToString());
    }

    [Theory]
    [InlineData("index.html")]
    [InlineData("visualcore.wasm")]
    [InlineData("visualcore.js")]
    [InlineData("favicon.ico")]
    [InlineData("Inter-Regular.ttf")]
    public void Unhashed_files_are_revalidated(string fileName)
    {
        var response = new DefaultHttpContext().Response;

        ClientCaching.Apply(response, fileName);

        Assert.Equal(ClientCaching.Revalidate, response.Headers.CacheControl.ToString());
    }
}
