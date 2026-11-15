namespace Canvas.Server.Storage;

public sealed class StorageOptions
{
    public const string Section = "Storage";

    /// <summary>SQLite file path, absolute or relative to the content root.</summary>
    public string Database { get; set; } = "data/canvas.db";

    /// <summary>Folder of the font file store, absolute or relative to the content root.</summary>
    public string Fonts { get; set; } = "data/fonts";
}
