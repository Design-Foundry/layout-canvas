namespace Canvas.Server.Icons;

/// <summary>One glyph of an icon set.</summary>
public sealed record IconGlyph(string Glyph);

/// <summary>A page of glyph names. <paramref name="Total"/> counts every match, not just this page.</summary>
public sealed record IconSearchResponse(string Version, int Total, int Offset, IReadOnlyList<IconGlyph> Items);

/// <summary>The axis values of one Material Symbols SVG: <c>style</c> is outlined, rounded or sharp.</summary>
public sealed record IconVariant(string Style, int Fill, int Weight, int OpticalSize)
{
    public static readonly IconVariant Default = new("outlined", 0, 400, 24);

    public static readonly IReadOnlyList<string> Styles = ["outlined", "rounded", "sharp"];
    public static readonly IReadOnlyList<int> Weights = [100, 200, 300, 400, 500, 600, 700];
    public static readonly IReadOnlyList<int> OpticalSizes = [20, 24, 40, 48];

    public bool IsOffered => Styles.Contains(Style) && Fill is 0 or 1 && Weights.Contains(Weight) && OpticalSizes.Contains(OpticalSize);

    /// <summary>The registry key native icon registration uses, as <c>vc_icon_source_key</c> writes it.</summary>
    public string SourceKey(string glyph) => $"material-symbols/{glyph}?style={Style}&FILL={Fill}&wght={Weight}&opsz={OpticalSize}";

    /// <summary>The file name under <c>symbols/web/&lt;glyph&gt;/materialsymbols&lt;style&gt;/</c>: <c>arrow_back_wght500fill1_24px.svg</c>.</summary>
    public string FileName(string glyph) =>
        $"{glyph}{(Weight != 400 || Fill == 1 ? "_" : "")}{(Weight != 400 ? $"wght{Weight}" : "")}{(Fill == 1 ? "fill1" : "")}_{OpticalSize}px.svg";
}
