using System.Text.Json.Serialization;
using Canvas.Server.Documents;
using Canvas.Server.Fonts;
using Canvas.Server.Mcp;
using Canvas.Server.Storage;

var builder = WebApplication.CreateBuilder(args);

builder.Services.AddOpenApi();
builder.Services.AddProblemDetails();
builder.Services.AddValidation();
builder.Services.ConfigureHttpJsonOptions(options => options.SerializerOptions.NumberHandling = JsonNumberHandling.Strict);

builder.Services.AddSingleton(TimeProvider.System);
builder.Services.Configure<StorageOptions>(builder.Configuration.GetSection(StorageOptions.Section));
builder.Services.AddSingleton<Database>();
builder.Services.AddHostedService<DatabaseInitializer>();
builder.Services.AddHttpClient(GoogleFontsProvider.HttpClientName)
    .ConfigurePrimaryHttpMessageHandler(() => new SocketsHttpHandler { AllowAutoRedirect = false });
builder.Services.Configure<GoogleFontsOptions>(builder.Configuration.GetSection(GoogleFontsOptions.Section));
builder.Services.AddSingleton<FontFileStore>();
builder.Services.AddSingleton<GoogleFontsProvider>();
builder.Services.AddSingleton<GoogleFontsRegistrar>();
builder.Services.AddSingleton<DocumentStore>();
builder.Services.Configure<DocumentHostOptions>(builder.Configuration.GetSection(DocumentHostOptions.Section));
builder.Services.AddSingleton<DocumentHost>();
builder.Services.AddSingleton<EditorSelections>();
builder.Services.AddSingleton<RenderRequests>();
builder.Services.AddCanvasMcp();

var app = builder.Build();

app.UseExceptionHandler();
app.UseStatusCodePages();

if (app.Environment.IsDevelopment())
{
    app.MapOpenApi();
}

var api = app.MapGroup("/api");

api.MapGet("/health", () => TypedResults.Ok(new HealthResponse("ok", typeof(Program).Assembly.GetName().Version?.ToString() ?? "0.0.0")))
    .WithName("GetHealth");

api.MapDocuments();
api.MapFonts();
app.MapCanvasMcp();

app.Run();

public sealed record HealthResponse(string Status, string Version);
