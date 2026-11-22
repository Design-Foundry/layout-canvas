using Microsoft.AspNetCore.Http.HttpResults;

namespace Canvas.Server.Icons;

public static class IconEndpoints
{
    public static RouteGroupBuilder MapIcons(this IEndpointRouteBuilder routes)
    {
        var icons = routes.MapGroup("/icons").WithTags("Icons");

        icons.MapGet("/material-symbols/glyphs", SearchGlyphs).WithName(nameof(SearchGlyphs));
        icons.MapGet("/material-symbols/glyphs/{glyph}", GetGlyph).WithName(nameof(GetGlyph))
            .Produces<string>(StatusCodes.Status200OK, "image/svg+xml");

        return icons;
    }

    /// <summary>
    /// Material Symbols glyph names that start with, then contain, <paramref name="query"/>, a page at a time.
    /// <paramref name="version"/> is a google/material-design-icons commit, the configured one when omitted.
    /// </summary>
    private static async Task<Results<Ok<IconSearchResponse>, ValidationProblem, ProblemHttpResult>> SearchGlyphs(
        string? query, int? offset, int? limit, string? version, MaterialSymbolsProvider provider, CancellationToken ct)
    {
        var commit = version ?? provider.DefaultCommit;
        if (!MaterialSymbolsProvider.IsValidCommit(commit))
        {
            return Invalid("version", "The version is a 40 character lowercase hex google/material-design-icons commit.");
        }
        if (offset is < 0)
        {
            return Invalid("offset", "The offset is zero or more.");
        }
        try
        {
            return TypedResults.Ok(await provider.SearchAsync(query, offset ?? 0, Math.Clamp(limit ?? 50, 1, 200), commit, ct));
        }
        catch (IconsUnavailableException e)
        {
            return Unavailable(e);
        }
    }

    /// <summary>
    /// The SVG of a Material Symbols glyph. <paramref name="style"/> is outlined, rounded or sharp; <paramref name="fill"/> 0 or 1;
    /// <paramref name="wght"/> 100 to 700 in steps of 100; <paramref name="opsz"/> 20, 24, 40 or 48. A glyph the source lacks is a 404.
    /// </summary>
    private static async Task<Results<FileContentHttpResult, NotFound, ValidationProblem, ProblemHttpResult>> GetGlyph(
        string glyph, string? style, int? fill, int? wght, int? opsz, string? version, HttpContext context,
        MaterialSymbolsProvider provider, CancellationToken ct)
    {
        var commit = version ?? provider.DefaultCommit;
        var variant = new IconVariant(style ?? IconVariant.Default.Style, fill ?? IconVariant.Default.Fill,
            wght ?? IconVariant.Default.Weight, opsz ?? IconVariant.Default.OpticalSize);
        var errors = new Dictionary<string, string[]>();
        if (!MaterialSymbolsProvider.IsValidCommit(commit))
        {
            errors["version"] = ["The version is a 40 character lowercase hex google/material-design-icons commit."];
        }
        if (!MaterialSymbolsProvider.IsValidGlyph(glyph))
        {
            errors["glyph"] = ["A glyph is lowercase letters, digits and underscores, up to 64 characters."];
        }
        if (!IconVariant.Styles.Contains(variant.Style))
        {
            errors["style"] = ["The style is outlined, rounded or sharp."];
        }
        if (variant.Fill is not (0 or 1))
        {
            errors["fill"] = ["FILL is 0 or 1."];
        }
        if (!IconVariant.Weights.Contains(variant.Weight))
        {
            errors["wght"] = ["wght is 100 to 700 in steps of 100."];
        }
        if (!IconVariant.OpticalSizes.Contains(variant.OpticalSize))
        {
            errors["opsz"] = ["opsz is 20, 24, 40 or 48."];
        }
        if (errors.Count > 0)
        {
            return TypedResults.ValidationProblem(errors);
        }
        try
        {
            if (await provider.GetSvgAsync(glyph, variant, commit, ct) is not { } svg)
            {
                return TypedResults.NotFound();
            }
            context.Response.Headers.ContentSecurityPolicy = "default-src 'none'; style-src 'unsafe-inline'; sandbox";
            context.Response.Headers.XContentTypeOptions = "nosniff";
            context.Response.Headers.CacheControl = version is null ? "public, max-age=3600" : "public, max-age=31536000, immutable";
            return TypedResults.File(System.Text.Encoding.UTF8.GetBytes(svg), "image/svg+xml");
        }
        catch (IconsUnavailableException e)
        {
            return Unavailable(e);
        }
    }

    private static ValidationProblem Invalid(string field, string message) =>
        TypedResults.ValidationProblem(new Dictionary<string, string[]> { [field] = [message] });

    private static ProblemHttpResult Unavailable(Exception e) =>
        TypedResults.Problem(e.Message, statusCode: StatusCodes.Status502BadGateway, title: "Material Symbols is not available");
}
