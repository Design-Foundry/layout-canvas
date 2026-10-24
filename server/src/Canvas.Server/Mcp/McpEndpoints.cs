using System.Net;

namespace Canvas.Server.Mcp;

public static class McpEndpoints
{
    public static IServiceCollection AddCanvasMcp(this IServiceCollection services)
    {
        services.AddMcpServer(options => options.ServerInfo = new() { Name = "canvas", Version = "0.1.0" })
            .WithHttpTransport()
            .WithTools<CanvasTools>();
        return services;
    }

    /// <summary>Maps the MCP endpoint at <c>/mcp</c>, answering only requests from this machine.</summary>
    public static IEndpointConventionBuilder MapCanvasMcp(this IEndpointRouteBuilder app) =>
        app.MapMcp("/mcp").AddEndpointFilter(async (context, next) =>
            context.HttpContext.Connection.RemoteIpAddress is { } address && IPAddress.IsLoopback(address)
                ? await next(context)
                : Results.StatusCode(StatusCodes.Status403Forbidden));
}
