using Microsoft.AspNetCore.Http.HttpResults;

namespace Canvas.Server.Fonts;

public static class FontEndpoints
{
    public const string Sha256Header = "X-Font-SHA256";

    public static RouteGroupBuilder MapFonts(this IEndpointRouteBuilder routes)
    {
        var fonts = routes.MapGroup("/fonts").WithTags("Fonts");

        fonts.MapGet("/google/families", ListGoogleFamilies).WithName(nameof(ListGoogleFamilies));
        fonts.MapPost("/google/pin", PinGoogleFamily).WithName(nameof(PinGoogleFamily));
        fonts.MapGet("/googlefonts/{version}/{key}/files", GetGoogleFontFiles).WithName(nameof(GetGoogleFontFiles));
        fonts.MapGet("/googlefonts/{version}/{key}/files/{file}", GetGoogleFontFile).WithName(nameof(GetGoogleFontFile))
            .Produces<byte[]>(StatusCodes.Status200OK, "font/ttf");

        return fonts;
    }

    /// <summary>Google Fonts families at the configured commit whose name contains <paramref name="query"/>.</summary>
    private static async Task<Results<Ok<GoogleFamiliesResponse>, ProblemHttpResult>> ListGoogleFamilies(
        string? query, int? limit, GoogleFontsProvider google, CancellationToken ct)
    {
        var take = Math.Clamp(limit ?? 50, 1, 200);
        try
        {
            var commit = google.DefaultCommit;
            var names = await google.GetCatalogAsync(commit, ct);
            var matches = names
                .Where(n => string.IsNullOrWhiteSpace(query) || n.Contains(query.Trim(), StringComparison.OrdinalIgnoreCase))
                .Take(take)
                .Select(n => new GoogleFamily(n))
                .ToList();
            return TypedResults.Ok(new GoogleFamiliesResponse(commit, matches));
        }
        catch (GoogleFontsUnavailableException e)
        {
            return Unavailable(e);
        }
    }

    /// <summary>Describes a Google Fonts family pinned to the configured commit, ready to store as a document font.</summary>
    private static async Task<Results<Ok<PinFontResponse>, NotFound, ValidationProblem, ProblemHttpResult>> PinGoogleFamily(
        PinFontRequest request, GoogleFontsProvider google, CancellationToken ct)
    {
        if (!GoogleFontsProvider.IsValidKey(request.Family?.Trim()))
        {
            return TypedResults.ValidationProblem(new Dictionary<string, string[]> { ["family"] = ["Provide a Google Fonts family name."] });
        }
        try
        {
            return await google.PinAsync(request.Family!.Trim(), google.DefaultCommit, ct) is var (font, fallback)
                ? TypedResults.Ok(new PinFontResponse(font, fallback))
                : TypedResults.NotFound();
        }
        catch (Exception e) when (e is GoogleFontsUnavailableException or FontFileException)
        {
            return Unavailable(e);
        }
    }

    /// <summary>The files of a Google Fonts family at a google/fonts commit, with the hash of each.</summary>
    private static async Task<Results<Ok<IReadOnlyList<FontFileEntry>>, NotFound, ValidationProblem, ProblemHttpResult>> GetGoogleFontFiles(
        string version, string key, GoogleFontsProvider google, CancellationToken ct)
    {
        if (Validate(version, key) is { } invalid)
        {
            return invalid;
        }
        try
        {
            return await google.GetManifestAsync(version, key, ct) is { } manifest ? TypedResults.Ok(manifest) : TypedResults.NotFound();
        }
        catch (Exception e) when (e is GoogleFontsUnavailableException or FontFileException)
        {
            return Unavailable(e);
        }
    }

    /// <summary>The bytes of one file listed by the family's manifest, with its SHA-256 in <c>X-Font-SHA256</c>.</summary>
    private static async Task<Results<FileContentHttpResult, NotFound, ValidationProblem, ProblemHttpResult>> GetGoogleFontFile(
        string version, string key, string file, HttpContext context, GoogleFontsProvider google, CancellationToken ct)
    {
        if (Validate(version, key) is { } invalid)
        {
            return invalid;
        }
        try
        {
            if (await google.GetFileAsync(version, key, file, ct) is not var (sha256, bytes))
            {
                return TypedResults.NotFound();
            }
            context.Response.Headers[Sha256Header] = sha256;
            context.Response.Headers.CacheControl = "public, max-age=31536000, immutable";
            return TypedResults.File(bytes, "font/ttf");
        }
        catch (Exception e) when (e is GoogleFontsUnavailableException or FontFileException)
        {
            return Unavailable(e);
        }
    }

    private static ValidationProblem? Validate(string version, string key)
    {
        var errors = new Dictionary<string, string[]>();
        if (!GoogleFontsProvider.IsValidCommit(version))
        {
            errors["version"] = ["The version is a 40 character lowercase hex google/fonts commit."];
        }
        if (!GoogleFontsProvider.IsValidKey(key))
        {
            errors["key"] = ["The key is an exact Google Fonts family name."];
        }
        return errors.Count == 0 ? null : TypedResults.ValidationProblem(errors);
    }

    private static ProblemHttpResult Unavailable(Exception e) =>
        TypedResults.Problem(e.Message, statusCode: StatusCodes.Status502BadGateway, title: "Google Fonts is not available");
}
