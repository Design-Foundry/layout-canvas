using System.Text.Json.Serialization;

namespace Canvas.Server.Fonts;

/// <summary>A continuous range (<see cref="Min"/> and <see cref="Max"/>) or a discrete list (<see cref="Values"/>).</summary>
public sealed record FontAxisDomain(
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] double? Min,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] double? Max,
    [property: JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)] IReadOnlyList<double>? Values);

public sealed record FontAxisCapability(string Tag, FontAxisDomain Domain, double Default);

public sealed record FontCapabilitiesInfo(IReadOnlyList<FontAxisCapability> Axes, IReadOnlyList<string> Features);

/// <summary>A font as a document stores it, without the <c>id</c> and <c>order</c> the client assigns.</summary>
public sealed record PinnedFont(
    string Identifier,
    string DisplayName,
    string Provider,
    string Key,
    string FamilyVersion,
    FontCapabilitiesInfo Capabilities);

public sealed record PinFontRequest(string Family);

public sealed record PinFontResponse(PinnedFont Font, string GenericFallback);

public sealed record GoogleFamily(string Family);

public sealed record GoogleFamiliesResponse(string Version, IReadOnlyList<GoogleFamily> Families);

/// <summary>One file of a family: the face id native layout knows it by, the axis values it is fixed at, and its hash.</summary>
public sealed record FontFileEntry(string Face, IReadOnlyDictionary<string, double> Fixed, string Sha256, string File);
