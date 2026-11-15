using System.Globalization;
using System.Text;

namespace Canvas.Server.Fonts;

/// <summary>One <c>fonts</c> entry of a <c>METADATA.pb</c>: a file with its style and weight.</summary>
public sealed record GoogleFontsFile(string Filename, string Style, double Weight);

/// <summary>One <c>axes</c> entry: the range a variable family ships.</summary>
public sealed record GoogleFontsAxis(string Tag, double Min, double Max);

/// <summary>The parts of a google/fonts <c>METADATA.pb</c> that Canvas uses.</summary>
public sealed record GoogleFontsMetadata(
    string Name,
    string Category,
    IReadOnlyList<GoogleFontsFile> Files,
    IReadOnlyList<GoogleFontsAxis> Axes)
{
    public bool IsVariable => Axes.Count > 0;

    /// <exception cref="FormatException">The text is not protobuf text, or lacks a name or files.</exception>
    public static GoogleFontsMetadata Parse(string text)
    {
        var position = 0;
        var root = ParseBlock(text, ref position, topLevel: true);

        var files = root.Blocks("fonts")
            .Select(f => new GoogleFontsFile(
                f.Scalar("filename") ?? throw new FormatException("A fonts entry has no filename."),
                f.Scalar("style") ?? "normal",
                Number(f.Scalar("weight")) ?? 400))
            .ToList();
        var axes = root.Blocks("axes")
            .Select(a => new GoogleFontsAxis(
                a.Scalar("tag") ?? throw new FormatException("An axes entry has no tag."),
                Number(a.Scalar("min_value")) ?? 0,
                Number(a.Scalar("max_value")) ?? 0))
            .ToList();
        var name = root.Scalar("name") ?? throw new FormatException("METADATA.pb has no name.");
        if (files.Count == 0)
        {
            throw new FormatException("METADATA.pb lists no fonts.");
        }
        return new GoogleFontsMetadata(name, root.Scalar("category") ?? "", files, axes);
    }

    private static double? Number(string? value) =>
        double.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out var number) ? number : null;

    private sealed class Block
    {
        private readonly List<(string Key, object Value)> entries = [];

        public void Add(string key, object value) => entries.Add((key, value));

        public IEnumerable<Block> Blocks(string key) => entries.Where(e => e.Key == key).Select(e => e.Value).OfType<Block>();

        public string? Scalar(string key) => entries.FirstOrDefault(e => e.Key == key && e.Value is string).Value as string;
    }

    private static Block ParseBlock(string text, ref int position, bool topLevel)
    {
        var block = new Block();
        while (true)
        {
            SkipTrivia(text, ref position);
            if (position >= text.Length)
            {
                return topLevel ? block : throw new FormatException("A block is not closed.");
            }
            if (text[position] == '}')
            {
                return topLevel ? throw new FormatException("Unexpected closing brace.") : block;
            }

            var start = position;
            while (position < text.Length && (char.IsLetterOrDigit(text[position]) || text[position] == '_'))
            {
                position++;
            }
            if (position == start)
            {
                throw new FormatException($"Expected a field name at {position}.");
            }
            var key = text[start..position];
            SkipTrivia(text, ref position);

            if (position < text.Length && text[position] == '{')
            {
                position++;
                var child = ParseBlock(text, ref position, topLevel: false);
                position++;
                block.Add(key, child);
            }
            else if (position < text.Length && text[position] == ':')
            {
                position++;
                SkipTrivia(text, ref position);
                block.Add(key, ReadScalar(text, ref position));
            }
            else
            {
                throw new FormatException($"Expected ':' or '{{' after {key}.");
            }
        }
    }

    private static string ReadScalar(string text, ref int position)
    {
        if (position < text.Length && text[position] == '"')
        {
            position++;
            var value = new StringBuilder();
            while (position < text.Length && text[position] != '"')
            {
                if (text[position] == '\\' && position + 1 < text.Length)
                {
                    position++;
                }
                value.Append(text[position++]);
            }
            if (position >= text.Length)
            {
                throw new FormatException("A string is not closed.");
            }
            position++;
            return value.ToString();
        }
        var start = position;
        while (position < text.Length && !char.IsWhiteSpace(text[position]) && text[position] is not ('#' or '}'))
        {
            position++;
        }
        return text[start..position];
    }

    private static void SkipTrivia(string text, ref int position)
    {
        while (position < text.Length)
        {
            if (char.IsWhiteSpace(text[position]))
            {
                position++;
            }
            else if (text[position] == '#')
            {
                while (position < text.Length && text[position] != '\n')
                {
                    position++;
                }
            }
            else
            {
                return;
            }
        }
    }
}
