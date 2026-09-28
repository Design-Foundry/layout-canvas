var builder = WebApplication.CreateBuilder(args);

builder.Services.AddOpenApi();

var app = builder.Build();

if (app.Environment.IsDevelopment())
{
    app.MapOpenApi();
}

var api = app.MapGroup("/api");

api.MapGet("/health", () => TypedResults.Ok(new HealthResponse("ok", typeof(Program).Assembly.GetName().Version?.ToString() ?? "0.0.0")))
    .WithName("GetHealth");

app.Run();

public sealed record HealthResponse(string Status, string Version);
