using System.Text.Json.Serialization;
using Canvas.Server;
using Canvas.Server.Documents;
using Canvas.Server.Fonts;
using Canvas.Server.Icons;
using Canvas.Server.Mcp;
using Canvas.Server.Storage;

var packaged = Directory.Exists(Path.Combine(AppContext.BaseDirectory, "wwwroot"));
var builder = WebApplication.CreateBuilder(new WebApplicationOptions
{
    Args = args,
    ContentRootPath = packaged ? AppContext.BaseDirectory : null,
});

if (packaged)
{
    if (Environment.GetEnvironmentVariable("ASPNETCORE_URLS") is null)
    {
        builder.WebHost.UseUrls("http://localhost:5180");
    }

    if (Environment.GetEnvironmentVariable("Storage__Database") is null)
    {
        builder.Configuration["Storage:Database"] = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Canvas", "canvas.db");
    }

    if (Environment.GetEnvironmentVariable("Storage__Fonts") is null)
    {
        builder.Configuration["Storage:Fonts"] = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Canvas", "fonts");
    }

    if (Environment.GetEnvironmentVariable("Storage__Icons") is null)
    {
        builder.Configuration["Storage:Icons"] = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Canvas", "icons");
    }
}

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
builder.Services.AddHttpClient(MaterialSymbolsProvider.HttpClientName)
    .ConfigurePrimaryHttpMessageHandler(() => new SocketsHttpHandler { AllowAutoRedirect = false });
builder.Services.Configure<MaterialSymbolsOptions>(builder.Configuration.GetSection(MaterialSymbolsOptions.Section));
builder.Services.AddSingleton<IconFileStore>();
builder.Services.AddSingleton<MaterialSymbolsProvider>();
builder.Services.AddSingleton<IconRegistrar>();
builder.Services.AddSingleton<DocumentStore>();
builder.Services.Configure<DocumentHostOptions>(builder.Configuration.GetSection(DocumentHostOptions.Section));
builder.Services.AddSingleton<DocumentHost>();
builder.Services.AddSingleton<EditorSelections>();
builder.Services.AddSingleton<RenderRequests>();
builder.Services.AddCanvasMcp();

var app = builder.Build();

app.UseExceptionHandler();
app.UseStatusCodePages();

var serveClient = app.Environment.WebRootPath is { } webRoot && Directory.Exists(webRoot);
var clientFiles = new StaticFileOptions { OnPrepareResponse = context => ClientCaching.Apply(context.Context.Response, context.File.Name) };
if (serveClient)
{
    app.UseDefaultFiles();
    app.UseStaticFiles(clientFiles);
    app.UseRouting();
}

if (app.Environment.IsDevelopment())
{
    app.MapOpenApi();
}

var api = app.MapGroup("/api");

api.MapGet("/health", () => TypedResults.Ok(new HealthResponse("ok", typeof(Program).Assembly.GetName().Version?.ToString() ?? "0.0.0")))
    .WithName("GetHealth");

api.MapDocuments();
api.MapFonts();
api.MapIcons();
app.MapCanvasMcp();

if (serveClient)
{
    app.MapFallbackToFile("{**path:regex(^(?!api(/|$)|mcp(/|$))[^.]*$)}", "index.html", clientFiles);
}

if (packaged && Environment.GetEnvironmentVariable("CANVAS_NO_BROWSER") is null)
{
    app.Lifetime.ApplicationStarted.Register(() =>
    {
        var address = app.Urls.FirstOrDefault();
        if (address is not null)
        {
            System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(address) { UseShellExecute = true });
        }
    });
}

app.Run();

public sealed record HealthResponse(string Status, string Version);
